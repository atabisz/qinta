#include "qt_window.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QString>
#include <QWheelEvent>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "app.h"
#include "file_dialog.h"
#include "file_utils.h"
#include "input.h"
#include "paint_document.h"
#include "qt_init.h"
#include "qt_key_map.h"
#include "render.h"
#include "render_qt.h"
#include "repaint_sink.h"
#include "scroll.h"
#include "scrollbar.h"
#include "text_api.h"
#include "text_utils.h"
#include "window_sink.h"
#include "zen_mode_qt.h"

namespace {

// Deliberately not localised through tr(). The i18n table is keyed by the app's own
// string ids and this line is only reachable when App holds no document -- adding it
// to the catalogue would leave a translated string behind for a screen that exists
// only until a file is opened.
//
// REWORDED, because the old text said the document renderer "is not on this stack
// yet" and that is no longer true: this window draws documents now, and the line is
// what an EMPTY window says.
const wchar_t kPlaceholder[] =
    L"tinta - no document open.";

// One less than the array extent, which is the terminator. Computed rather than
// written: the seam takes an explicit length, and a hand-typed count on a string
// somebody will reword is a defect waiting for the reword.
constexpr uint32_t kPlaceholderLength =
    static_cast<uint32_t>(sizeof(kPlaceholder) / sizeof(kPlaceholder[0]) - 1);

// Matches the body size the start page uses. Not read from the theme, because
// D2DTheme carries font FAMILIES and no sizes -- every size in this tree is a
// literal at its call site.
constexpr float kPlaceholderFontSize = 18.0f;

// The half-height of the band the line is centred in. Generous, because the test
// samples pixels inside it and a band tight to the glyph box would make the test
// depend on whichever font the platform substituted.
constexpr float kPlaceholderBandHalfHeight = 40.0f;

// Qt reports wheel rotation in eighths of a degree; 120 of them is one notch, the same
// unit WHEEL_DELTA defines on Windows. Dividing here is what lets src/scroll.cpp take
// notches and be shared with src/input.cpp, which divides by WHEEL_DELTA for the same
// reason.
constexpr float kAngleDeltaPerNotch = 120.0f;

}  // namespace

QtAppWindow::QtAppWindow(App& app, QWidget* parent) : QWidget(parent), app_(app) {
    // WITHOUT THIS THE WINDOW NEVER RECEIVES A KEY EVENT. QWidget defaults to
    // Qt::NoFocus, so keyPressEvent below would compile, be reachable from a test that
    // posts an event directly, pass, and never once fire in the running application.
    // StrongFocus rather than ClickFocus: the window has to be usable from the keyboard
    // before anything has been clicked, because it is launched with a file in it.
    setFocusPolicy(Qt::StrongFocus);

    // WITHOUT THIS, EVERY ONE OF THE 277 requestRepaint CALLS UNDER src/ IS A NO-OP.
    // The seam in include/win32_api.h has no way to reach a QWidget on its own -- it is
    // included tree-wide, including by translation units the Windows build compiles
    // without Qt on the include path -- so the window hands it a closure instead. This
    // is the same failure shape as the focus policy above: without it the code compiles,
    // the calls return, App really does change, and nothing is ever redrawn.
    //
    // update() rather than repaint(): update() coalesces multiple requests into one
    // paint at the next opportunity, which is what InvalidateRect does on the Windows
    // arm. repaint() paints synchronously, so a handler that requests a repaint twice --
    // and input.cpp has paths that do -- would paint twice per keystroke here and once
    // on Windows. The seam's whole claim is that the two arms are the same request.
    tinta::setRepaintSink([this]() { update(); });

    // THE SAME FAILURE SHAPE A THIRD TIME, for the key router's two window requests. Without
    // this, Escape with nothing open and Q both call requestQuit, which returns false and
    // does nothing; F11 flips app_.zenMode, gets an invalid restore back, and flips it off
    // again -- the key works in every sense a test of App could see and the window never
    // leaves its frame. include/window_sink.h is the seam.
    //
    // QCoreApplication::quit() rather than close(): the Win32 arm is PostQuitMessage, which
    // ends the message loop rather than destroying one window, and there is one window.
    tinta::WindowSink sink;
    sink.quit = []() { QCoreApplication::quit(); };
    sink.zenEnter = [this]() { return tinta::zenModeEnter(*this); };
    sink.zenLeave = [this](const tinta::ZenRestore& restore) {
        return tinta::zenModeLeave(*this, restore);
    };
    tinta::setWindowSink(std::move(sink));
}

QtAppWindow::~QtAppWindow() {
    // Unconditional, and correct only because there is one window: see the header's
    // single-window note. A second QtAppWindow would have displaced this sink already,
    // and clearing here would then be removing somebody else's.
    tinta::clearRepaintSink();
    tinta::clearWindowSink();
}

bool QtAppWindow::syncViewport() {
    const int w = width();
    const int h = height();
    const bool changed = app_.width != w || app_.height != h;
    app_.width = w;
    app_.height = h;
    return changed;
}

bool QtAppWindow::openDocument(const std::string& path) {
    if (!loadDocumentFromPath(app_, path)) return false;

    // fromUtf8, NOT fromLocal8Bit, and the two disagree on Windows for a non-ASCII
    // path. loadDocumentFromPath converts the same bytes with toWide, which is a
    // UTF-8 conversion, so this is the spelling that makes the title and the file that
    // actually opened agree with each other. The remaining wrinkle is real and is
    // upstream of here: main() passes argv[1], which the Microsoft runtime hands over
    // in the ANSI code page, so a non-ASCII path on the command line will fail to open
    // AND show mojibake -- one cause, two consistent symptoms, fixed by
    // CommandLineToArgvW at the entry point rather than by guessing here. The dialog
    // path below has no such problem: it arrives as a wstring and is converted once.
    setWindowTitle(QStringLiteral("%1 - tinta").arg(QString::fromUtf8(path.c_str())));
    update();
    return true;
}

void QtAppWindow::promptOpenDocument() {
    platform::FileDialogRequest request;
    // No owner. HWND is a Windows type and the Qt backend ignores it, which makes the
    // dialog application-modal -- see src/file_dialog_qt.cpp.
    request.owner = nullptr;
    // Copied verbatim from editor.cpp:1064's openFileDialog, so Ctrl+O offers the same
    // three filters in the same order on both stacks. A shorter list here would be a
    // divergence in the part of the product a user sees first.
    request.filters = {
        {L"Documents (*.md;*.mmd;*.txt;*.json;*.yaml;...)",
         L"*.md;*.markdown;*.mmd;*.txt;*.json;*.yaml;*.yml;*.toml;"
         L"*.ini;*.csv;*.log"},
        {L"Markdown / Mermaid (*.md;*.markdown;*.mmd)", L"*.md;*.markdown;*.mmd"},
        {L"All files (*.*)", L"*.*"}};
    request.restoreDirectory = true;
    request.hideReadOnly = true;

    std::optional<platform::FileDialogResult> picked = platform::requestOpenPath(request);
    if (!picked) return;
    // A path that will not open leaves the current document on screen. openDocument's
    // return is deliberately dropped: there is no status bar on this stack to put a
    // message in, and stderr is not where a GUI user looks.
    openDocument(toUtf8(picked->path));
}

void QtAppWindow::wheelEvent(QWheelEvent* event) {
    // Ctrl+wheel is ZOOM on Windows (input.cpp:878, applyZoomDelta) and is not
    // implemented here. Ignored rather than treated as a scroll, so the gesture does
    // nothing instead of doing the wrong thing -- and ignore() rather than accept() so
    // a parent that does implement it would still see it.
    if (event->modifiers() & Qt::ControlModifier) {
        event->ignore();
        return;
    }

    // Before the arithmetic, not after: every scroll maximum comes from app_.height,
    // and the first wheel event can arrive before the first paint.
    syncViewport();

    const float vertical = static_cast<float>(event->angleDelta().y()) / kAngleDeltaPerNotch;
    const float horizontal = static_cast<float>(event->angleDelta().x()) / kAngleDeltaPerNotch;

    if (event->modifiers() & Qt::ShiftModifier) {
        // Shift+wheel is horizontal, matching input.cpp:879-885. The vertical delta is
        // what a shifted wheel reports, so `vertical` is the right input here.
        tinta::scrollDocumentHorizontallyByNotches(app_, vertical);
    } else {
        if (vertical != 0.0f) tinta::scrollDocumentByNotches(app_, vertical);
        // A tilt wheel or a trackpad's horizontal axis. THE SIGN HERE IS UNVERIFIED ON
        // THIS HOST -- there is no tilt wheel to test with -- and is taken from Qt's
        // documented convention that a positive x delta is a rotation to the LEFT,
        // which is the same direction a positive vertical notch means, hence the same
        // helper with no negation. Win32's WM_MOUSEHWHEEL uses the opposite sign
        // (input.cpp:900 adds where :881 subtracts), so this is exactly the kind of
        // detail a port gets backwards; it is called out rather than left to be
        // discovered as a "scrolling goes the wrong way" report.
        if (horizontal != 0.0f) tinta::scrollDocumentHorizontallyByNotches(app_, horizontal);
    }

    event->accept();
    update();
}

void QtAppWindow::keyPressEvent(QKeyEvent* event) {
    // Ctrl+O first and HERE, not in handleKeyDown, for two reasons. handleKeyDown's Ctrl+O
    // reads platform::isCtrlDown(), which on this stack is QGuiApplication::keyboardModifiers()
    // (src/keyboard_qt.cpp) -- the state of the last input event Qt itself processed -- so a
    // synthesised key event, which every test in tests/qt_entry_tests.cpp sends, would reach
    // it with Ctrl reported up. And this window's
    // own openDocument retitles the window, which the shared path does not do yet.
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_O) {
        promptOpenDocument();
        event->accept();
        return;
    }

    // Before the handlers, not after: every scroll maximum they compute comes from
    // app_.height, and the first key can arrive before the first paint.
    syncViewport();

    // THE ROUTER. The same two calls, in the same order, that the Win32 message loop makes
    // for one keystroke: WM_KEYDOWN reaches handleKeyDown, and TranslateMessage's WM_CHAR
    // reaches handleCharInput UNLESS handleKeyDown returned true (include/input.h, #195).
    // The HWND is nullptr, which is what it is on this stack everywhere (App::hwnd is only
    // ever assigned by src/main_d2d.cpp), and every seam it reaches takes its off-stack
    // arm for that reason -- see TINTA_QT_UI in CMakeLists.txt.
    //
    // THE 12-KEY SWITCH THAT STOOD HERE IS GONE RATHER THAN KEPT AS A FALLBACK. Every one of
    // its keys maps to a code handleKeyDown's viewer branch already handles, with the same
    // numbers (src/scroll.h names them), and handleKeyDown returns "consume the character",
    // not "handled" -- so there is no signal a fallback could run on, and running it
    // unconditionally would scroll twice per keystroke.
    const unsigned vk = tinta::vkFromQtKey(event->key());
    const QString text = event->text();
    if (vk == 0 && text.isEmpty()) {
        // Nothing either handler could match. The base class, so a parent sees it.
        QWidget::keyPressEvent(event);
        return;
    }

    bool consumed = false;
    if (vk != 0) consumed = handleKeyDown(app_, nullptr, vk);
    if (!consumed) {
        // One call per UTF-16 unit, which is what WM_CHAR delivers: a character outside
        // the BMP arrives as two messages. So handleCharInput sees here exactly the units
        // it sees on Windows -- whatever it does with a surrogate, it does on both stacks.
        for (const QChar unit : text) {
            handleCharInput(app_, nullptr, static_cast<wchar_t>(unit.unicode()));
        }
    }

    event->accept();
    update();
}

tinta::RectF QtAppWindow::placeholderRect() const {
    const float w = static_cast<float>(width());
    const float mid = static_cast<float>(height()) * 0.5f;
    // left, top, right, bottom -- RectF inherited Direct2D's shape, and getting that
    // wrong produces a plausible-looking rectangle rather than an error.
    return tinta::RectF{0.0f, mid - kPlaceholderBandHalfHeight, w,
                        mid + kPlaceholderBandHalfHeight};
}

void QtAppWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // THE LAYOUT IS MARKED DIRTY, NOT PERFORMED. layoutDocument walks the whole element
    // tree, and Qt delivers a resize event per frame of a window drag -- doing the work
    // here would relayout a long document sixty times a second while the user is still
    // dragging. Windows takes the same shape: WM_SIZE sets app.layoutDirty and the paint
    // that follows does the work.
    //
    // WHAT THIS ACTUALLY FIXES, because syncViewport was already called from paintDocument
    // and a reader is entitled to ask what was broken: the SCROLL POSITION. Every scroll
    // maximum is contentHeight - app_.height, so a TALLER window has a SMALLER maximum --
    // GROWING the window is what leaves app_.scrollY past the end, and shrinking one never
    // can. This sentence said "making a window shorter" until tests/qt_entry_tests.cpp's
    // resize run was written, which is the direction the arithmetic refutes and the reason
    // the test asserts a grow: an assertion written to match the wrong sentence would have
    // measured a clamp that never fires. clampDocumentScroll fixes it here, against the new
    // height, before anything paints.
    if (syncViewport()) {
        app_.layoutDirty = true;
        // AFTER the flag and BEFORE the paint. The clamp reads app_.contentHeight, which
        // is still the OLD document height at this point -- the relayout has not run --
        // so this is a clamp against a stale extent, and it is still the right call: a
        // stale maximum is a bounded amount wrong, whereas no clamp at all leaves the
        // scroll offset unbounded. The paint's own prepareDocumentViewport clamps again
        // against the fresh extent, so the final position is correct either way and this
        // only removes the one-frame flash.
        tinta::clampDocumentScroll(app_);
    }
    update();
}

bool QtAppWindow::paintDocument() {
    if (!app_.root) return false;

    // Synced here as well as in resizeEvent, and that is not redundant: a widget can be
    // resized before it is shown, in which case no resize event has been delivered yet.
    // The input handlers call it too, because every scroll maximum is derived from
    // app_.height and a wheel event before the first paint would clamp against zero.
    const bool resized = syncViewport();

    // Relayout on a size change, and on the first paint after a document is set --
    // which is what an empty run list means here, since every document tinta can open
    // produces at least one text run. Not "always": layoutDocument walks the whole
    // element tree and this is the paint path.
    //
    // app_.layoutDirty is in the test now because resizeEvent sets it. `resized` alone
    // would still be true in the common case, but only because syncViewport happens to be
    // called from both places -- a resize followed by anything that syncs first would
    // have left the flag set and the layout stale.
    if (resized || app_.layoutDirty || app_.layoutTextRuns.empty()) {
        layoutDocument(app_);
    }

    // Nothing to show. Distinguished from "no document" by the caller only in that
    // both fall back to the placeholder; a document that lays out to nothing is a
    // layout defect, and drawing a blank window would hide it.
    if (app_.layoutTextRuns.empty() && app_.layoutRects.empty() &&
        app_.layoutShapes.empty() && app_.layoutBitmaps.empty() &&
        app_.layoutLines.empty()) {
        return false;
    }

    // AFTER the layout and BEFORE the paint, which is the order main_d2d.cpp uses and the
    // only order that works: it reads app_.contentHeight, and it latches visibility so an
    // incremental layout cannot make the right edge blink.
    updateScrollbarExtent(app_);

    // Held rather than passed inline, because the scrollbars need the same object. Two
    // prepareDocumentViewport calls would be worse than untidy -- it MUTATES App
    // (scrollX, scrollY, targetScrollY, pendingScrollRestore), so the second call would
    // be clamping against state the first one already moved.
    const DocumentViewport viewport = prepareDocumentViewport(app_);
    paintDocumentLayout(app_, viewport);

    // The chrome the Windows paint handler drew inline until this slice: the same rounded
    // thumb, the same 6-to-10 DIP hover swell, the same colour rule. It draws OVER the
    // document rather than reserving a column, which is why it comes after.
    //
    // The hover and drag flags it reads are set by src/input.cpp's mouse handling, which
    // is Windows-only, so on this stack the thumb is always in its resting form and cannot
    // be dragged. A missing caller, not a missing implementation -- see include/scrollbar.h.
    paintDocumentScrollbars(app_, viewport);
    return true;
}

void QtAppWindow::paintEvent(QPaintEvent* event) {
    (void)event;
    paintCount_++;
    documentPainted_ = false;

    // The QPainter lives exactly as long as this object, which is exactly as long as
    // this function. That is the whole reason QtScopedRenderTarget exists rather than
    // an install/release pair the way the text factory has.
    QtScopedRenderTarget target(app_, *this);
    renderTargetInstalled_ = target.target() != nullptr;
    if (!renderTargetInstalled_) return;

    // Every line below goes through app_.renderTarget, the pointer the 566 drawing
    // sites in the tree read. Nothing here names QtRenderTarget.
    app_.renderTarget->Clear(app_.theme.background);

    // No text factory means no text, and it means it QUIETLY. That is not an
    // oversight being tolerated: tests/qt_entry_tests.cpp releases the factory and
    // grabs again, and the band coming back as flat background is what makes the
    // text assertion in the run before it discriminating rather than decorative.
    if (!app_.textFactory) return;

    // A document if there is one, the placeholder if there is not. The order matters
    // for the control in tests/qt_entry_tests.cpp: releasing the text factory has to
    // stop BOTH branches drawing, or the control measures nothing.
    documentPainted_ = paintDocument();
    if (documentPainted_) return;

    std::unique_ptr<tinta::TextFormat> format = app_.textFactory->CreateTextFormat(
        app_.theme.fontFamily, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
        tinta::FontStretch::Normal, kPlaceholderFontSize, L"en-us");
    if (!format) return;
    format->SetTextAlignment(tinta::TextAlignment::Center);
    format->SetParagraphAlignment(tinta::ParagraphAlignment::Center);

    // Constructed here rather than held as a member: a brush is one colour and the
    // theme can change under a running window, so caching one would be a cache with
    // an invalidation rule and nothing to gain.
    QtBrush brush(app_.theme.text);
    app_.renderTarget->DrawText(kPlaceholder, kPlaceholderLength, format.get(),
                                placeholderRect(), &brush);
}

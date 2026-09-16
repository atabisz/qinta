#include "qt_window.h"

#include <QKeyEvent>
#include <QPaintEvent>
#include <QString>
#include <QWheelEvent>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "app.h"
#include "file_dialog.h"
#include "file_utils.h"
#include "paint_document.h"
#include "qt_init.h"
#include "render.h"
#include "render_qt.h"
#include "scroll.h"
#include "text_api.h"
#include "text_utils.h"

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
    // Ctrl+wheel is ZOOM on Windows (input.cpp:874, applyZoomDelta) and is not
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
        // Shift+wheel is horizontal, matching input.cpp:875-881. The vertical delta is
        // what a shifted wheel reports, so `vertical` is the right input here.
        tinta::scrollDocumentHorizontallyByNotches(app_, vertical);
    } else {
        if (vertical != 0.0f) tinta::scrollDocumentByNotches(app_, vertical);
        // A tilt wheel or a trackpad's horizontal axis. THE SIGN HERE IS UNVERIFIED ON
        // THIS HOST -- there is no tilt wheel to test with -- and is taken from Qt's
        // documented convention that a positive x delta is a rotation to the LEFT,
        // which is the same direction a positive vertical notch means, hence the same
        // helper with no negation. Win32's WM_MOUSEHWHEEL uses the opposite sign
        // (input.cpp:895 adds where :877 subtracts), so this is exactly the kind of
        // detail a port gets backwards; it is called out rather than left to be
        // discovered as a "scrolling goes the wrong way" report.
        if (horizontal != 0.0f) tinta::scrollDocumentHorizontallyByNotches(app_, horizontal);
    }

    event->accept();
    update();
}

void QtAppWindow::keyPressEvent(QKeyEvent* event) {
    // Ctrl+O first: the modifier check has to come before the plain-key switch or
    // Ctrl+J would scroll.
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_O) {
            promptOpenDocument();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    syncViewport();

    bool handled = true;
    switch (event->key()) {
        // K and J are vi bindings, and they are input.cpp's, not an addition here.
        case Qt::Key_Up:
        case Qt::Key_K:
            tinta::scrollDocumentByStep(app_, -1);
            break;
        case Qt::Key_Down:
        case Qt::Key_J:
            tinta::scrollDocumentByStep(app_, 1);
            break;
        case Qt::Key_Left:
            tinta::scrollDocumentHorizontallyByStep(app_, -1);
            break;
        case Qt::Key_Right:
            tinta::scrollDocumentHorizontallyByStep(app_, 1);
            break;
        case Qt::Key_PageUp:
            tinta::scrollDocumentByPage(app_, -1);
            break;
        case Qt::Key_PageDown:
        case Qt::Key_Space:
            tinta::scrollDocumentByPage(app_, 1);
            break;
        case Qt::Key_Home:
            tinta::scrollDocumentToTop(app_);
            break;
        case Qt::Key_End:
            tinta::scrollDocumentToBottom(app_);
            break;
        default:
            handled = false;
            break;
    }

    if (!handled) {
        // Escape, Tab and everything else this stack does not implement yet has to
        // reach the base class, or the window swallows keys it does nothing with.
        QWidget::keyPressEvent(event);
        return;
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

bool QtAppWindow::paintDocument() {
    if (!app_.root) return false;

    // App's viewport is the widget's size. It is synced here rather than in a resize
    // handler because this stack has no resize handler yet -- see the header comment.
    // The input handlers call syncViewport too, and they have to: a scroll maximum is
    // derived from app_.height, so a wheel event before the first paint would clamp
    // against zero.
    const bool resized = syncViewport();

    // Relayout on a size change, and on the first paint after a document is set --
    // which is what an empty run list means here, since every document tinta can open
    // produces at least one text run. Not "always": layoutDocument walks the whole
    // element tree and this is the paint path.
    if (resized || app_.layoutTextRuns.empty()) {
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

    paintDocumentLayout(app_, prepareDocumentViewport(app_));
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

#include "qt_window.h"

#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QString>
#include <QStyleHints>
#include <QTimer>
#include <QWheelEvent>

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <optional>

#include "app.h"
#include "drafts.h"
#include "file_dialog.h"
#include "file_utils.h"
#include "input.h"
#include "keyboard.h"
#include "paint_document.h"
#include "qt_init.h"
#include "qt_key_map.h"
#include "qt_launch.h"
#include "render.h"
#include "render_qt.h"
#include "post_sink.h"
#include "repaint_sink.h"
#include "scroll.h"
#include "scrollbar.h"
#include "text_api.h"
#include "text_utils.h"
#include "theme_apply.h"
#include "timer_sink.h"
#include "win32_api.h"  // CursorShape's definition; window_sink.h only declares it
#include "window_sink.h"
#include "zen_mode_qt.h"
#ifdef _WIN32
#include "window_chrome_win32.h"  // the chrome sink paints through DWM on this host only
#endif

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
// unit WHEEL_DELTA defines on Windows. Dividing here hands handleMouseWheel and
// handleMouseHWheel the notches WinMain gives them after dividing by WHEEL_DELTA.
constexpr float kAngleDeltaPerNotch = 120.0f;

// The four-way mapping include/win32_api.h has named since the seam was written. SplitH
// for SizeWE because the one SizeWE user is the edit/preview divider, and Qt's split cursor
// is the one drawn for exactly that; SizeHorCursor is the window-edge resize arrow.
Qt::CursorShape qtCursorFor(CursorShape shape) {
    switch (shape) {
        case CursorShape::Hand: return Qt::PointingHandCursor;
        case CursorShape::IBeam: return Qt::IBeamCursor;
        case CursorShape::SizeWE: return Qt::SplitHCursor;
        case CursorShape::Arrow:
        default: return Qt::ArrowCursor;
    }
}

// Every handler below that reaches src/input.cpp opens one of these first, so the Shift and
// Ctrl questions input.cpp asks are answered by THIS event, and not by Qt's cached state or
// GetKeyState, which a sent mouse or wheel event does not reach. include/keyboard.h has the
// measurement and says why the scope is two bools.
platform::EventModifierScope modifierScopeFor(const QInputEvent& event) {
    return platform::EventModifierScope(event.modifiers().testFlag(Qt::ShiftModifier),
                                        event.modifiers().testFlag(Qt::ControlModifier));
}

}  // namespace

QtAppWindow::QtAppWindow(App& app, QWidget* parent) : QWidget(parent), app_(app) {
    // WITHOUT THIS THE WINDOW NEVER RECEIVES A KEY EVENT. QWidget defaults to
    // Qt::NoFocus, so keyPressEvent below would compile, be reachable from a test that
    // posts an event directly, pass, and never once fire in the running application.
    // StrongFocus rather than ClickFocus: the window has to be usable from the keyboard
    // before anything has been clicked, because it is launched with a file in it.
    setFocusPolicy(Qt::StrongFocus);

    // THE MOUSE COUNTERPART, same failure shape: without it Qt delivers mouseMoveEvent only
    // while a button is held, so hover -- link and tab highlights, and every cursor shape
    // below -- would work during a drag and nowhere else. WM_MOUSEMOVE arrives with no
    // button down, and handleMouseMove is written for that.
    setMouseTracking(true);

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
    // The 58 setCursorShape calls in input.cpp and editor.cpp. setCursor rather than
    // QGuiApplication::setOverrideCursor: SetCursor on Windows is per-window-under-the-
    // pointer, which is what a widget cursor is, and an override would outlive the window.
    sink.cursor = [this](CursorShape shape) { setCursor(qtCursorFor(shape)); };
    // applyTheme's title bar (slice 13). Recorded on every platform so a test can see the
    // delivery; painted only on Windows, where winId() is the HWND DWM wants. Off Windows the
    // frame is the window manager's -- see src/theme_qt.cpp.
    sink.chrome = [this](const tinta::WindowChrome& chrome) {
        ++chromeCount_;
        lastChrome_ = chrome;
#ifdef _WIN32
        applyDwmWindowChrome(reinterpret_cast<HWND>(winId()), chrome);
#endif
    };
    tinta::setWindowSink(std::move(sink));
    // Once now, because the first applyTheme ran in applySavedTheme before this window existed
    // and delivered to nobody. winId() creates the native window early; that is harmless for a
    // top-level widget and is what lets the first frame already show the theme's caption.
    tinta::deliverWindowChrome(windowChromeFor(app_));

    // AND A FOURTH TIME, for armTimer and disarmTimer (include/timer_sink.h). Without it
    // every timer the tree arms is dropped, and the one that showed it is TIMER_ZOOM_APPLY:
    // app_.zoomApplyPending would never clear, so the first Ctrl+wheel notch would zoom and
    // every later one would only rescale the scroll offset.
    //
    // One QTimer per id, kept for the window's lifetime, which is what makes it SetTimer:
    // start() on a running QTimer restarts it at the new interval, a QTimer repeats until
    // stopped, and stop() on an idle one does nothing. The tick is input.cpp's handleTimer,
    // the dispatch WM_TIMER calls, so the two stacks run the same code per id.
    tinta::TimerSink timerSink;
    timerSink.arm = [this](unsigned id, unsigned intervalMs) {
        QTimer*& timer = timers_[id];
        if (!timer) {
            timer = new QTimer(this);
            QObject::connect(timer, &QTimer::timeout, this, [this, id]() {
                // Same argument as the input handlers: the drag auto-scroll tick clamps
                // against app_.height.
                syncViewport();
                handleTimer(app_, nullptr, id);
            });
        }
        timer->start(static_cast<int>(intervalMs));
    };
    timerSink.disarm = [this](unsigned id) {
        const auto found = timers_.find(id);
        if (found != timers_.end()) found->second->stop();
    };
    tinta::setTimerSink(std::move(timerSink));

    // AND A FIFTH, for postWindowMessage (include/post_sink.h), which is the only sink a
    // WORKER thread calls: the async image download and the folder scan post their results
    // from the threads that produced them. So this closure runs off the GUI thread, reads
    // nothing of app_, and does one thing -- queue the handler onto this widget's thread,
    // which is what PostMessageW's queue does for WinMain. A message input.cpp has no
    // handler for gets false, and its sender keeps ownership of whatever lParam points at.
    //
    // The update() after the handler is belt and braces: completeFolderSearch repaints
    // through canRepaint itself now, and completeAsyncImage's early exits do not.
    //
    // WM_CLOSE is queued like the others -- its senders are mid-handler, and closing under
    // them is what the queue exists to prevent -- but it is answered by close(), which
    // runs closeEvent and so the same unsaved-buffer check WinMain's WM_CLOSE runs.
    //
    // WM_SETTINGCHANGE is the follow-system toggle posting to itself, with a string literal
    // for lParam, so there is nothing to own. It takes WinMain's case with this arm's reading.
    tinta::setPostSink([this](unsigned message, std::uintptr_t wParam, std::intptr_t lParam) {
        if (!handlesPostedMessage(message)) return false;
        return QMetaObject::invokeMethod(
            this,
            [this, message, wParam, lParam]() {
                if (message == WM_CLOSE) {
                    close();
                    return;
                }
                if (message == WM_SETTINGCHANGE) {
                    const auto* area = reinterpret_cast<const wchar_t*>(lParam);
                    if (area && std::wcscmp(area, L"ImmersiveColorSet") == 0 &&
                        followSystemAppearance(app_, tinta::systemPrefersLight())) {
                        update();
                    }
                    return;
                }
                syncViewport();
                handlePostedMessage(app_, message, static_cast<WPARAM>(wParam),
                                    static_cast<LPARAM>(lParam));
                update();
            },
            Qt::QueuedConnection);
    });

    // The system's own appearance change, WinMain's WM_SETTINGCHANGE from outside. Qt 6.5
    // added the signal; below it there is none, so a follow-on window there changes only when
    // the toggle is used or the app starts.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                     [this](Qt::ColorScheme scheme) {
                         if (followSystemAppearance(app_, scheme != Qt::ColorScheme::Dark)) {
                             update();
                         }
                     });
#endif
}

QtAppWindow::~QtAppWindow() {
    // FIRST, because it is the one a worker thread can be inside right now: clearPostSink
    // takes the slot's lock, so when it returns no closure over `this` is still running.
    // A post already queued onto this widget is dropped with it by Qt, and so is the
    // allocation it carries -- one window, process lifetime, accepted rather than drained.
    tinta::clearPostSink();
    // Unconditional, and correct only because there is one window: see the header's
    // single-window note. A second QtAppWindow would have displaced this sink already,
    // and clearing here would then be removing somebody else's.
    tinta::clearRepaintSink();
    tinta::clearWindowSink();
    // The QTimers themselves are children of this widget and go with it.
    tinta::clearTimerSink();
}

bool QtAppWindow::timerActive(unsigned id) const {
    const auto found = timers_.find(id);
    return found != timers_.end() && found->second->isActive();
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

// THE WHEEL, ROUTED (slice 4). WinMain sends WM_MOUSEWHEEL to handleMouseWheel and
// WM_MOUSEHWHEEL to handleMouseHWheel, each in notches; Qt carries both axes in one event, so
// each non-zero axis makes the call its own message would. The modifier scope is what made
// this routable: handleMouseWheel's Shift branch asks platform::isShiftDown.
//
// Ctrl+wheel is routed too, as of slice 5. Slice 4 ignored it here because handleMouseWheel
// sends it to applyZoomDelta, which coalesces notches through TIMER_ZOOM_APPLY, and armTimer
// was a no-op on this stack -- so app_.zoomApplyPending would never have cleared. The timer
// sink the constructor installs is what clears it, and the lightbox's Ctrl+wheel with it.
void QtAppWindow::wheelEvent(QWheelEvent* event) {
    // Before the call, not after: every scroll maximum comes from app_.height, and the first
    // wheel event can arrive before the first paint.
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);

    const float vertical = static_cast<float>(event->angleDelta().y()) / kAngleDeltaPerNotch;
    const float horizontal = static_cast<float>(event->angleDelta().x()) / kAngleDeltaPerNotch;

    if (vertical != 0.0f) handleMouseWheel(app_, nullptr, vertical);
    // NEGATED, and the sign is still unverified on this host -- there is no tilt wheel to test
    // with. Qt's documented convention is that a positive x delta is a rotation to the LEFT;
    // WM_MOUSEHWHEEL's positive delta is a tilt to the RIGHT, and handleMouseHWheel ADDS it to
    // targetScrollX. So Qt's number is the opposite of the one Windows hands that function.
    if (horizontal != 0.0f) handleMouseHWheel(app_, nullptr, -horizontal);

    event->accept();
    update();
}

void QtAppWindow::keyPressEvent(QKeyEvent* event) {
    // Ctrl+O first and HERE, not in handleKeyDown, because this window's own openDocument
    // retitles the window, which the shared path does not do yet. There used to be a second
    // reason -- handleKeyDown could not see Ctrl on a synthesised event -- and the modifier
    // scope below retired it. That reason was only ever true on Windows, where handleKeyDown
    // asks GetKeyState; Qt's cache does follow a sent key event (include/keyboard.h).
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
    // numbers, and handleKeyDown returns "consume the character",
    // not "handled" -- so there is no signal a fallback could run on, and running it
    // unconditionally would scroll twice per keystroke.
    const unsigned vk = tinta::vkFromQtKey(event->key());
    const QString text = event->text();
    if (vk == 0 && text.isEmpty()) {
        // Nothing either handler could match. The base class, so a parent sees it.
        QWidget::keyPressEvent(event);
        return;
    }

    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
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

// THE MOUSE ROUTER. Each handler makes the one call WinMain's matching message makes
// (src/main_d2d.cpp), with the position in this widget's logical pixels -- the same space
// syncViewport gives app_.width/height, so hit tests and the viewport agree. No update()
// here, unlike keyPressEvent: WM_MOUSEMOVE does not invalidate, the handlers request their
// own repaints through the sink, and a repaint per hover move would redraw constantly.
//
// Only the LEFT button presses and releases, because WinMain handles only WM_LBUTTON*; the
// right button reaches handleContextMenu through contextMenuEvent, which is WM_CONTEXTMENU's
// counterpart, and the middle button (tab-strip close) is out of this slice's scope.
void QtAppWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    const QPoint p = event->position().toPoint();
    handleMouseDown(app_, nullptr, p.x(), p.y());
    event->accept();
}

// A SECOND PRESS, NOT A DOUBLE-CLICK. tinta's window class has no CS_DBLCLKS, so Windows
// delivers the second click of a pair as another WM_LBUTTONDOWN, and handleMouseDown counts
// clicks itself (500 ms, 5 px). Qt replaces that second press with this event, so it goes
// down the same path -- the default already would, via QWidget's own override, but that is
// an implementation detail of Qt this router should not lean on.
void QtAppWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    mousePressEvent(event);
}

void QtAppWindow::mouseMoveEvent(QMouseEvent* event) {
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    const QPoint p = event->position().toPoint();
    handleMouseMove(app_, nullptr, p.x(), p.y());
    event->accept();
}

void QtAppWindow::mouseReleaseEvent(QMouseEvent* event) {
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    const QPoint p = event->position().toPoint();
    switch (event->button()) {
        case Qt::LeftButton:
            handleMouseUp(app_, nullptr, p.x(), p.y());
            break;
        // WM_XBUTTONUP's two arms: on RELEASE, as Windows does it, so a press that is
        // dragged off the window and released elsewhere navigates nowhere.
        case Qt::BackButton:
            navigateBack(app_, nullptr);
            break;
        case Qt::ForwardButton:
            navigateForward(app_, nullptr);
            break;
        default:
            QWidget::mouseReleaseEvent(event);
            return;
    }
    event->accept();
}

void QtAppWindow::contextMenuEvent(QContextMenuEvent* event) {
    syncViewport();
    // Keyboard is the menu key or Shift+F10 -- WM_CONTEXTMENU's (-1, -1). handleContextMenu
    // then ignores the position and centres the menu, so pos() is passed either way.
    const bool fromKeyboard = event->reason() == QContextMenuEvent::Keyboard;
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    handleContextMenu(app_, nullptr, event->pos().x(), event->pos().y(), fromKeyboard);
    event->accept();
}

tinta::RectF QtAppWindow::placeholderRect() const {
    const float w = static_cast<float>(width());
    const float mid = static_cast<float>(height()) * 0.5f;
    // left, top, right, bottom -- RectF inherited Direct2D's shape, and getting that
    // wrong produces a plausible-looking rectangle rather than an error.
    return tinta::RectF{0.0f, mid - kPlaceholderBandHalfHeight, w,
                        mid + kPlaceholderBandHalfHeight};
}

void QtAppWindow::closeEvent(QCloseEvent* event) {
    // The dialog windowCloseMayProceed opens is App state drawn by the paint, so a refusal
    // repaints through the repaint sink it already called; nothing else to do here.
    if (windowCloseMayProceed(app_, nullptr)) {
        // WinMain's WM_DESTROY, in its order: every dirty buffer was just resolved, so a
        // leftover draft would resurrect content the user already decided about -- then the
        // save, narrowed to the fields this arm loads (qt_launch.h).
        draftsDeleteAll(app_);
        tinta::saveClosingState(app_, *this);
        event->accept();
    } else {
        event->ignore();
    }
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

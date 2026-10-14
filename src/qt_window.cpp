#include "qt_window.h"

#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QList>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QString>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app.h"
#include "drafts.h"
#include "file_dialog.h"
#include "file_utils.h"
#include "input.h"
#include "keyboard.h"
#include "menu_bar_qt.h"  // keyStaysWithWindow
#include "overlays.h"
#include "paint_document.h"
#include "render_frame.h"
#include "qt_init.h"
#include "qt_key_map.h"
#include "qt_launch.h"
#include "render.h"
#include "render_qt.h"
#include "post_sink.h"
#include "repaint_sink.h"
#include "scroll.h"
#include "scrollbar.h"
#include "tab_ghost_qt.h"
#include "utils.h"  // updateWindowTitle, the one caption computer
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

// THE PLACEHOLDER LINE THAT STOOD HERE ("tinta - no document open.", 18 DIP) IS GONE: since
// 2026-10-06 the window draws tinta's frame, and with no document that frame is the start page.
// placeholderRect() below survives as a measurement band -- the centre of the window, where the
// start page draws -- because tests/qt_entry_tests.cpp samples it.
//
// The half-height of the band the line is centred in. Generous, because the test
// samples pixels inside it and a band tight to the glyph box would make the test
// depend on whichever font the platform substituted.
constexpr float kPlaceholderBandHalfHeight = 40.0f;

// Qt reports wheel rotation in eighths of a degree; 120 of them is one notch, the same
// unit WHEEL_DELTA defines on Windows. Dividing here hands handleMouseWheel and
// handleMouseHWheel the notches the retired WinMain gave them after dividing by WHEEL_DELTA.
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
    // The native title bar keeps its own caption buttons on this window, so tinta's tab strip
    // must not draw a second set (include/app.h, App::nativeCaption).
    app_.nativeCaption = true;
    // WITHOUT THIS THE WINDOW NEVER RECEIVES A KEY EVENT. QWidget defaults to
    // Qt::NoFocus, so keyPressEvent below would compile, be reachable from a test that
    // posts an event directly, pass, and never once fire in the running application.
    // StrongFocus rather than ClickFocus: the window has to be usable from the keyboard
    // before anything has been clicked, because it is launched with a file in it.
    setFocusPolicy(Qt::StrongFocus);
    // THE INPUT METHOD, same failure shape again: without this attribute Qt keeps an input
    // method off for the widget, and a CJK one cannot compose into the editor at all. Whether
    // it is on at a given moment is inputMethodQuery(ImEnabled)'s answer, editor only.
    setAttribute(Qt::WA_InputMethodEnabled, true);

    // THE MOUSE COUNTERPART, same failure shape: without it Qt delivers mouseMoveEvent only
    // while a button is held, so hover -- link and tab highlights, and every cursor shape
    // below -- would work during a drag and nowhere else. WM_MOUSEMOVE arrives with no
    // button down, and handleMouseMove is written for that.
    setMouseTracking(true);

    // THE DROP COUNTERPART, and the same failure shape a third time: a QWidget receives no
    // drag or drop event whatever unless this is set, so dragEnterEvent and dropEvent below
    // would compile, be reachable from a test that sends an event directly, pass, and never
    // fire once in the running application. This is DragAcceptFiles(hwnd, TRUE), which
    // main_d2d.cpp called at window creation.
    setAcceptDrops(true);

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
    // The caption (2026-10-12). THIS IS QWidget::setWindowTitle, the call win32_api.h's
    // setWindowTitle comment named as "the Qt path" for the whole port while its own body
    // discarded the argument. All 13 updateWindowTitle call sites arrive here now.
    //
    // A COUNTER, like chrome's, and for the same reason: "the caption is unchanged" and "no
    // sink was listening" are indistinguishable from the caption alone, and this member is
    // the one a window could install three of its siblings and still forget.
    sink.title = [this](const std::wstring& title) {
        ++titleCount_;
        setWindowTitle(QString::fromStdWString(title));
    };
    // releaseMouseIfCaptured (2026-10-12). THE GUARD IS THE POINT, not the release: this is
    // `if (GetCapture() == hwnd) ReleaseCapture();` translated, and without the comparison a
    // release routed from a bracketed call site could end a grab that belongs to another
    // widget. The one site that actually needs this is tabs.cpp's tabDragCancel, reached from
    // the Esc branch of handleKeyDown -- a KEYSTROKE, which does not end Qt's implicit grab,
    // so before this existed Esc aborted the drag's state while the widget kept the mouse.
    sink.releaseMouse = [this]() {
        if (QWidget::mouseGrabber() != this) return false;
        releaseMouse();
        return true;
    };
    // The tab strip's pin (slice 22). Set on the NATIVE window rather than through
    // QWidget::setWindowFlag, which hides a visible widget and needs a show() to come back --
    // a pin that blinks the window is not SetWindowPos(HWND_TOPMOST). Reports whether the flag
    // is now what was asked for, so the pin flips only on a request that landed.
    sink.stayOnTop = [this](bool on) {
        winId();  // creates the native window if it does not exist yet
        QWindow* native = windowHandle();
        if (!native) return false;
        native->setFlag(Qt::WindowStaysOnTopHint, on);
        return native->flags().testFlag(Qt::WindowStaysOnTopHint) == on;
    };
    // The tab drag's tear-off (slice 24): tinta's ClientToScreen, the drop's "is this point on
    // this window", and the ghost card, which src/tab_ghost_qt.cpp draws.
    sink.toScreen = [this](int x, int y, int& screenX, int& screenY) {
        const QPoint global = mapToGlobal(QPoint(x, y));
        screenX = global.x();
        screenY = global.y();
    };
    sink.containsScreen = [this](int screenX, int screenY) {
        return frameGeometry().contains(QPoint(screenX, screenY));
    };
    sink.ghostShow = [this](const tinta::TabGhostCard& card, const std::wstring& title,
                            int screenX, int screenY) {
        tinta::tabGhostDestroy(ghost_);
        if (testAttribute(Qt::WA_DontShowOnScreen)) {
            // A window that is not on screen gets a ghost that is not on screen either --
            // the test windows are built this way, and on Windows' real platform plugin an
            // ordinary show() would flash the card onto the desktop of whoever ran ctest.
            ghost_ = new tinta::TabGhostWidget(card, QString::fromStdWString(title));
            ghost_->setAttribute(Qt::WA_DontShowOnScreen, true);
            ghost_->move(screenX, screenY);
            ghost_->show();
        } else {
            ghost_ = tinta::tabGhostCreate(card, QString::fromStdWString(title), screenX,
                                           screenY);
        }
    };
    sink.ghostMove = [this](int screenX, int screenY) {
        if (ghost_) tinta::tabGhostMove(ghost_, screenX, screenY);
    };
    sink.ghostHide = [this]() { tinta::tabGhostDestroy(ghost_); };
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
                handleTimer(app_, id);
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
    // which is what PostMessageW's queue did for WinMain. A true hands the receiver
    // ownership of whatever `pointer` points at.
    //
    // The update() after the handler is belt and braces: completeFolderSearch repaints
    // through canRepaint itself now, and completeAsyncImage's early exits do not.
    //
    // Close is queued like the others -- its senders are mid-handler, and closing under
    // them is what the queue exists to prevent -- but it is answered by close(), which
    // runs closeEvent and so the same unsaved-buffer check WinMain's WM_CLOSE ran.
    //
    // SettingChange is the follow-system toggle posting to itself, with a string literal
    // for `pointer`, so there is nothing to own. It takes the retired WinMain's case with this arm's reading.
    //
    // Every tinta::PostedMessage is taken: the switch below has a case for each and no default,
    // as handlePostedMessage's does, so a message added without one fails the Linux build.
    tinta::setPostSink([this](tinta::PostedMessage message, std::uintptr_t value,
                              std::intptr_t pointer) {
        return QMetaObject::invokeMethod(
            this,
            [this, message, value, pointer]() {
                using tinta::PostedMessage;
                switch (message) {
                case PostedMessage::Close:
                    close();
                    return;
                case PostedMessage::SettingChange: {
                    const auto* area = reinterpret_cast<const wchar_t*>(pointer);
                    if (area && std::wcscmp(area, L"ImmersiveColorSet") == 0 &&
                        followSystemAppearance(app_, tinta::systemPrefersLight())) {
                        update();
                    }
                    return;
                }
                case PostedMessage::ImageReady:
                case PostedMessage::FolderSearch:
                case PostedMessage::LayoutChunk:
                case PostedMessage::PandocDone:
                    syncViewport();
                    handlePostedMessage(app_, message, value, pointer);
                    update();
                    return;
                }
            },
            Qt::QueuedConnection);
    });

    // The system's own appearance change, the retired WinMain's WM_SETTINGCHANGE from outside. Qt 6.5
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
    // A tab still detached when the window goes: its ghost is a top-level widget, not a child.
    tinta::tabGhostDestroy(ghost_);
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

    // updateWindowTitle, not a QWidget::setWindowTitle of its own (2026-10-12). What stood
    // here built `<full path> - Qinta` while src/utils.cpp built `Qinta - <file name>` for the
    // same document, and only this one was reachable off Windows -- so the caption's word
    // order and path form depended on whether a file arrived by Ctrl+O or by a tab switch.
    // Routing here instead also gets the dirty marker and the quick-note case, neither of
    // which this line knew about.
    //
    // ITS fromUtf8 ARGUMENT IS PRESERVED BY THE MOVE RATHER THAN DISCARDED, and that is why
    // this is a deletion and not a rewrite: the paragraph here argued for fromUtf8 over
    // fromLocal8Bit because loadDocumentFromPath converts the same bytes with toWide, which
    // is a UTF-8 conversion. updateWindowTitle reads app.currentFile through that very toWide,
    // so the agreement the argument asked for is now structural instead of a spelling choice
    // repeated at each site. main_qt.cpp's copy of this line had made the OPPOSITE choice,
    // fromLocal8Bit, and has gone with it.
    //
    // The wrinkle that argument named was real and is fixed at the entry point, not here:
    // main() parsed argv, which the Microsoft runtime hands over in the ANSI code page, so a
    // non-ASCII path on the command line failed to open. Since 2026-10-14 main() parses
    // tinta::launchArguments() (qt_launch.h), which is UTF-8 on every platform.
    updateWindowTitle(app_);
    update();
    return true;
}

void QtAppWindow::promptOpenDocument() {
    platform::FileDialogRequest request;
    // No owner, so the dialog is application-modal -- see include/file_dialog.h.
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

// THE WHEEL, ROUTED (slice 4). WinMain sent WM_MOUSEWHEEL to handleMouseWheel and
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

    if (vertical != 0.0f) handleMouseWheel(app_, vertical);
    // NEGATED, and the sign is still unverified on this host -- there is no tilt wheel to test
    // with. Qt's documented convention is that a positive x delta is a rotation to the LEFT;
    // WM_MOUSEHWHEEL's positive delta is a tilt to the RIGHT, and handleMouseHWheel ADDS it to
    // targetScrollX. So Qt's number is the opposite of the one Windows hands that function.
    if (horizontal != 0.0f) handleMouseHWheel(app_, -horizontal);

    event->accept();
    update();
}

bool QtAppWindow::event(QEvent* event) {
    // Before keyPressEvent, which is where these keys go once accepted. Without this a menu-bar
    // shortcut would take Cmd+A from the editor and the router would never see it.
    if (event->type() == QEvent::ShortcutOverride &&
        tinta::keyStaysWithWindow(*static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
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
    // ever assigned by the retired src/main_d2d.cpp), and every seam it reaches has one
    // body now, the sink-backed one.
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
    if (vk != 0) consumed = handleKeyDown(app_, vk);
    if (!consumed) {
        // One call per UTF-16 unit, which is what WM_CHAR delivers: a character outside
        // the BMP arrives as two messages. So handleCharInput sees here exactly the units
        // it sees on Windows -- whatever it does with a surrogate, it does on both stacks.
        for (const QChar unit : text) {
            handleCharInput(app_, static_cast<wchar_t>(unit.unicode()));
        }
    }

    event->accept();
    update();
}

// THE MOUSE ROUTER. Each handler makes the one call WinMain's matching message made
// (the retired src/main_d2d.cpp), with the position in this widget's logical pixels -- the same space
// syncViewport gives app_.width/height, so hit tests and the viewport agree. No update()
// here, unlike keyPressEvent: WM_MOUSEMOVE does not invalidate, the handlers request their
// own repaints through the sink, and a repaint per hover move would redraw constantly.
//
// Only the LEFT button presses and releases, because WinMain handled only WM_LBUTTON*; the
// right button reaches handleContextMenu through contextMenuEvent, which is WM_CONTEXTMENU's
// counterpart, and the middle button (tab-strip close) is out of this slice's scope.
void QtAppWindow::mousePressEvent(QMouseEvent* event) {
    // THE MIDDLE BUTTON CLOSES A TAB, as of 2026-10-12. It fell into the `!= LeftButton`
    // arm below and was handed to QWidget, so the README's middle-click-to-close worked on
    // no platform -- tabStripMouseDown's `middle` parameter had one caller passing a
    // hardcoded false. Its own entry point rather than a button argument on
    // handleMouseDown, which is the shape WinMain's separate `case WM_MBUTTONDOWN` had.
    //
    // ACCEPTED ONLY WHEN IT ACTED. The Win32 case returned 0 inside the strip and `break`
    // otherwise, so a middle press on the document is still the widget's to ignore -- which
    // leaves the door open for a future paste-on-middle-click without a conflict here.
    if (event->button() == Qt::MiddleButton) {
        syncViewport();
        const platform::EventModifierScope modifiers = modifierScopeFor(*event);
        const QPoint p = event->position().toPoint();
        if (handleMiddleMouseDown(app_, p.x(), p.y())) {
            event->accept();
        } else {
            QWidget::mousePressEvent(event);
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    const QPoint p = event->position().toPoint();
    handleMouseDown(app_, p.x(), p.y());
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
    handleMouseMove(app_, p.x(), p.y());
    event->accept();
}

void QtAppWindow::mouseReleaseEvent(QMouseEvent* event) {
    syncViewport();
    const platform::EventModifierScope modifiers = modifierScopeFor(*event);
    const QPoint p = event->position().toPoint();
    switch (event->button()) {
        case Qt::LeftButton:
            handleMouseUp(app_, p.x(), p.y());
            break;
        // WM_XBUTTONUP's two arms: on RELEASE, as Windows does it, so a press that is
        // dragged off the window and released elsewhere navigates nowhere.
        case Qt::BackButton:
            navigateBack(app_);
            break;
        case Qt::ForwardButton:
            navigateForward(app_);
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
    handleContextMenu(app_, event->pos().x(), event->pos().y(), fromKeyboard);
    event->accept();
}

// The local files on a drag payload, in the order the platform listed them, as native paths.
//
// NATIVE SEPARATORS, not QUrl::toLocalFile's output as it comes. On Windows that returns
// forward slashes, which open fine but then travel into App::currentFile, the window caption
// and settings.ini's [Recent] -- so the same document dropped and opened through Ctrl+O would
// be two different strings in the recent list.
//
// A NON-LOCAL URL YIELDS AN EMPTY PATH AND IS SKIPPED, which is what makes this the accept
// test as well as the conversion: a drag from a browser carries http URLs, a drag of selected
// text carries no URLs at all, and neither is a file this window can open.
static std::vector<std::wstring> droppedLocalPaths(const QMimeData* mime) {
    std::vector<std::wstring> paths;
    if (!mime || !mime->hasUrls()) return paths;
    const QList<QUrl> urls = mime->urls();
    paths.reserve(static_cast<std::size_t>(urls.size()));
    for (const QUrl& url : urls) {
        const QString local = url.toLocalFile();
        if (local.isEmpty()) continue;
        const QString native = QDir::toNativeSeparators(local);
        paths.emplace_back(native.toStdWString());
    }
    return paths;
}

void QtAppWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Accepted on the PRESENCE of a local file, never on its extension -- see the header.
    if (droppedLocalPaths(event->mimeData()).empty()) {
        QWidget::dragEnterEvent(event);
        return;
    }
    // acceptProposedAction rather than accept(): it records WHICH action was taken, so the
    // source sees a copy rather than a move and a file dragged out of Explorer is not
    // unlinked. WM_DROPFILES had no equivalent to get wrong -- the shell only ever offered a
    // copy.
    event->acceptProposedAction();
}

void QtAppWindow::dropEvent(QDropEvent* event) {
    const std::vector<std::wstring> paths = droppedLocalPaths(event->mimeData());
    if (paths.empty()) {
        QWidget::dropEvent(event);
        return;
    }
    syncViewport();
    // Unfiltered, as include/input.h:57 asks: handleDropFiles decides which ones it supports,
    // opens each into a tab with the first activating, and repaints only if something opened.
    // Skipping every path is therefore the same no-op a drop of nothing is.
    handleDropFiles(app_, paths);
    event->acceptProposedAction();
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
    if (windowCloseMayProceed(app_)) {
        // WinMain's WM_DESTROY, in its order: every dirty buffer was just resolved, so a
        // leftover draft would resurrect content the user already decided about -- then the
        // save: the preferences, the reading position, the session and the geometry (qt_launch.h).
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

    // No text factory means no text, and it means it QUIETLY. That is not an
    // oversight being tolerated: tests/qt_entry_tests.cpp releases the factory and
    // grabs again, and the window coming back as flat background is what makes the
    // text assertions before it discriminating rather than decorative.
    if (!app_.textFactory) {
        app_.renderTarget->Clear(app_.theme.background);
        return;
    }

    // WM_SIZE's half, done at paint time because a widget can be resized before it is shown,
    // when no resize event has been delivered yet. A changed viewport needs a relayout, which
    // is what WinMain's WM_SIZE asked for with the same flag.
    if (syncViewport()) app_.layoutDirty = true;

    // THE WHOLE FRAME, tinta's, since 2026-10-06: src/render_frame.cpp is the body that was
    // the retired main_d2d.cpp's render(), and this function is its bracket. Until then this window drew
    // the document and its scrollbars itself, and after the print slice the preview and the
    // link peek -- so the start page, the editor, the tab strip, search and every overlay and
    // dialog were invisible here while the key and mouse routers drove them. With no document
    // the frame is tinta's start page, which replaced this window's own placeholder line.
    renderFrame(app_);

    // Which branch the frame took, for tests/qt_entry_tests.cpp: a document, laid out to
    // something, and not hidden behind the print preview or the editor.
    documentPainted_ = app_.root && !app_.showPrintPreview && !app_.editMode &&
                       !(app_.layoutTextRuns.empty() && app_.layoutRects.empty() &&
                         app_.layoutShapes.empty() && app_.layoutBitmaps.empty() &&
                         app_.layoutLines.empty());

    // The frame just recorded where the caret is, and the editor may have opened or closed
    // since the last one, so the input method re-reads both: whether it may compose, and where
    // its candidate window goes.
    if (hasFocus()) {
        QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImCursorRectangle);
    }
}

QVariant QtAppWindow::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
        case Qt::ImEnabled:
            return app_.editMode;
        case Qt::ImCursorRectangle: {
            const tinta::RectF& r = app_.editorCaretRect;
            return QRectF(r.left, r.top, r.right - r.left, r.bottom - r.top).toAlignedRect();
        }
        default:
            return QWidget::inputMethodQuery(query);
    }
}

void QtAppWindow::inputMethodEvent(QInputMethodEvent* event) {
    if (!app_.editMode) {
        app_.imePreedit.clear();
        event->ignore();
        return;
    }
    syncViewport();
    // The commit first: an input method replacing its composition with the chosen text sends
    // the commit and an empty preedit in one event.
    for (const QChar unit : event->commitString()) {
        handleCharInput(app_, static_cast<wchar_t>(unit.unicode()));
    }
    app_.imePreedit = event->preeditString().toStdWString();
    event->accept();
    update();
}

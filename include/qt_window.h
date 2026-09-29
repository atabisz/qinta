#ifndef TINTA_QT_WINDOW_H
#define TINTA_QT_WINDOW_H

// The Qt window that hosts an App: the counterpart to the WndProc and the
// ID2D1HwndRenderTarget in src/main_d2d.cpp.
//
// WHY THE WINDOW IS A CLASS IN A HEADER rather than a lambda inside main(). A test
// has to be able to exercise the SAME class the application runs. tinta already has
// the other shape and knows what it costs: the Direct2D paint path lives inside
// main_d2d.cpp's WinMain and its window procedure, which is why no test on either
// platform has ever painted a tinta window and why every render assertion in the
// tree paints into a bitmap instead. tests/qt_entry_tests.cpp constructs this class,
// grabs it, and reads the pixels back.
//
// WHAT IT DRAWS TODAY: a DOCUMENT, when App has one, and the placeholder line when it
// does not. The sentence that stood here until this commit said the opposite -- "it
// does NOT draw a document, and it cannot yet -- src/render.cpp does not compile off
// Windows" -- and it was true when written and is now false in both halves.
// src/render.cpp compiles off Windows (asserted by the portable_compile_only target,
// which carries no Qt include paths at all), and the document paint pass came out of
// src/main_d2d.cpp into src/paint_document.cpp so that this class could call it.
//
// A CORRECTED COMMENT RATHER THAN A DELETED ONE, deliberately: the old claim named a
// blocker, the blocker moved, and a reader who remembers the old sentence needs to
// find out here that it changed rather than wonder whether they misremembered.
//
// MATH IS PORTED, AND THIS PARAGRAPH IS THE THIRD CORRECTION IN THIS COMMENT RATHER
// THAN A DELETION, for the reason the paragraph above gives. It read: "THE ONE THING NOT
// PORTED IS MATH. src/math_render.cpp is still Windows-only, so qinta links
// src/math_stub.cpp in its place and equations render as their own LaTeX source." Both
// halves are now false. src/math_render.cpp measures through app.textFactory and draws
// through a tinta::RenderTarget, src/math_stub.cpp is deleted, and this window typesets
// equations. What survives of the old claim is only its shape: an unparseable equation
// still falls back to its own source, because that is the contract for bad input and
// always was.
//
// WHY THE WINDOW LAYS OUT INSIDE paintEvent, which a shipping app should not do:
// there is no resize path on this stack yet. Windows relayouts on WM_SIZE. Until the
// Qt side has its own event wiring, the paint is the only place that knows the widget
// changed size, and a window that paints a stale layout after a resize would look
// like a broken renderer. Stated here because it is a known temporary and not a
// design.
//
// THAT COMMENT IS NOW HALF FALSE AND THE HALF THAT SURVIVES IS THE IMPORTANT ONE.
// There IS event wiring on this stack: wheelEvent and keyPressEvent scroll, and Ctrl+O
// opens a second document. The layout still happens inside paintEvent, and the reason
// has changed from "nothing else runs" to "syncViewport is called from three places and
// only the paint knows a relayout is affordable". A resizeEvent override is still owed.
//
// THE resizeEvent OVERRIDE IS PAID as of 2026-09-16, and the paragraph above is left
// standing because BOTH of its readings turned out to be right about the wrong thing.
// The layout still happens inside paintEvent, deliberately: Qt delivers a resize event
// per frame of a window drag, so relayouting there would walk the element tree sixty
// times a second while the pointer is still moving. resizeEvent sets app.layoutDirty and
// the paint does the work -- which is exactly WM_SIZE's shape, so the two stacks now agree
// rather than one of them being a temporary.
//
// What the override actually fixed was not the layout at all. It was the SCROLL POSITION:
// every maximum is contentHeight - app.height, so GROWING the window shrinks the maximum
// and leaves app.scrollY past the end until the next scroll input re-clamps it, and the
// document appears to scroll itself. Recorded because "a resizeEvent override is owed"
// named the right gap for the wrong reason, which is a thing this tree keeps doing to
// itself -- and this paragraph said "shortening the window" until the test that asserts it
// was written, which is the same mistake one level down: shrinking a window makes MORE of
// the document reachable and can never need a clamp.
//
// WHAT INPUT THIS WINDOW HANDLES, as of the key router: every KEY, through src/input.cpp.
// keyPressEvent translates the Qt key code (src/qt_key_map.cpp) and makes the same two
// calls the Win32 message loop makes, handleKeyDown and then handleCharInput, so the
// keyboard behaviours are the ones tinta has rather than a subset copied here. Ctrl+O is
// the one key kept local -- see keyPressEvent for why. The MOUSE, as of slice 3: press,
// move, release, double-click, the context menu and the two side buttons go through
// src/input.cpp the way WinMain routes them, and the cursor comes back through the window
// sink. The WHEEL, as of slice 4, goes to handleMouseWheel and handleMouseHWheel, so an
// open overlay takes it before the document; Ctrl+wheel zoom joined it in slice 5, once
// timers were ported -- see wheelEvent. Every routed handler opens a platform::EventModifierScope
// (include/keyboard.h) from its event, so Shift and Ctrl reach src/input.cpp from the event
// itself rather than from Qt's cache or GetKeyState, neither of which follows a sent mouse
// or wheel event.
// The one thing that MUST live here
// is setFocusPolicy: a QWidget defaults to Qt::NoFocus and therefore never receives a
// key event at all, which no pixel test and no probe in this tree could have caught --
// keyPressEvent is reachable from a test with sendEvent whether the widget can be
// focused or not, so the test asserts the focus policy separately and on purpose.
//
// WHERE THE REST OF THE TREE'S REPAINTS ARRIVE, as of 2026-09-21. This class installs
// the repaint sink (include/repaint_sink.h) in its constructor, so the 277
// requestRepaint call sites under src/ reach QWidget::update() off Windows instead of
// returning silently. That is the counterpart to setFocusPolicy above and fails the same
// way if it is missing: everything compiles, every call returns, and the screen never
// changes. The direct update() calls in this file are NOT replaced by it -- they are
// this window's own repaints of its own state, and routing them through a seam whose
// argument is a window handle this class does not have would buy nothing.
//
// AND WHERE requestQuit AND ZEN MODE ARRIVE, the same way: the constructor also installs
// the window sink (include/window_sink.h), so Escape, Q and F11 reach this widget
// instead of reading a null HWND and doing nothing.
//
// AND THE TIMERS, as of slice 5: the constructor installs the timer sink
// (include/timer_sink.h), one QTimer per id, and each tick runs src/input.cpp's
// handleTimer -- the dispatch WM_TIMER runs on Windows.
//
// AND THE POSTED MESSAGES, as of slice 8: the post sink (include/post_sink.h) queues
// WM_APP_IMAGE_READY and WM_APP_FOLDER_SEARCH from their worker threads onto this widget,
// where input.cpp's handlePostedMessage runs the handlers WinMain's window procedure calls.
// It also queues WM_CLOSE (slice 10) and WM_SETTINGCHANGE (slice 12), which the window
// answers itself: close(), and the follow-system switch with qt_launch's appearance reading.
// On Qt 6.5+ the constructor also connects colorSchemeChanged to that same switch.
//
// AND THE TITLE BAR, as of slice 13: the window sink's chrome member receives applyTheme's
// colours and, on Windows, paints them with the Direct2D stack's DWM calls on winId(). The
// constructor applies the current theme's once, because the first applyTheme ran before the
// window existed. Linux and macOS record the colours and paint nothing -- src/theme_qt.cpp.
//
// SINGLE WINDOW BY CONTRACT, and the contract is enforced elsewhere: each sink slot holds
// one window's closures, so a second QtAppWindow would displace the first's and the first
// window would stop repainting and stop answering F11. src/main_qt.cpp refuses --new and --tabbed today, which is what
// makes that safe rather than lucky. The slice that makes a second window possible owns
// turning the slot into a per-window dispatch, and this paragraph is the note it needs.

#include <QWidget>
#include <map>
#include <string>

#include "geometry.h"
#include "window_sink.h"  // tinta::WindowChrome

class QCloseEvent;
class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;
struct App;

class QtAppWindow final : public QWidget {
public:
    // The App is BORROWED and must outlive the window. That is main()'s job and the
    // test's, and it is the same contract QtRenderTarget has with its paint device.
    explicit QtAppWindow(App& app, QWidget* parent = nullptr);

    // DECLARED ONLY TO UNREGISTER THE SINKS (repaint, window, timer, post), the only things this class owns that
    // outlive it. The constructor installs closures capturing `this`, so a repaint or zen
    // request arriving after destruction would call into freed memory -- and the call
    // sites that make such requests are in files that know nothing about this window's
    // lifetime. A compiler-generated destructor would have left
    // that dangling, silently, in exactly the build where nothing crashes on the
    // happy path.
    ~QtAppWindow() override;

    // How many times paintEvent has run. A test that asserts on pixels without
    // asserting this cannot tell a correct paint from a paint that never happened
    // into a window that already held the right colour.
    int paintCount() const { return paintCount_; }

    // Whether the most recent paint got a non-null render target out of the storage.
    // Separate from paintCount() because "the paint event ran" and "the seam handed
    // back an implementation" are two claims, and only the second is about the port.
    bool renderTargetInstalled() const { return renderTargetInstalled_; }

    // The band the placeholder line is drawn into, in the widget's own coordinates.
    // Public because the test samples pixels inside it and must not re-derive the
    // geometry -- a test that computes the rectangle a second time is asserting its
    // own arithmetic agrees with itself.
    tinta::RectF placeholderRect() const;

    // Which of the two branches the most recent paint took.
    //
    // THIS IS A DISCRIMINATOR, not bookkeeping. Both branches draw dark text on the
    // theme background, so "pixels in the upper half differ from the background" is
    // satisfied by a document AND by a placeholder line that happens to be wide
    // enough. A pixel test with no way to tell the branches apart would report the
    // placeholder as a rendered document, which is precisely the class of false green
    // this tree keeps finding in its own instruments.
    bool documentPainted() const { return documentPainted_; }

    // Whether the timer sink holds timer `id` running -- armed and not yet disarmed. The
    // test reads it beside the App flag the timer clears, so "the flag is still set" can be
    // told apart from "the timer was never armed".
    bool timerActive(unsigned id) const;

    // How many title-bar colourings the chrome sink has received, and the last one. Read beside
    // the DWM attributes on Windows, and alone elsewhere, where nothing paints them -- so the
    // test can tell "delivered and not painted" (Linux, by design) from "never delivered".
    int chromeCount() const { return chromeCount_; }
    const tinta::WindowChrome& lastChrome() const { return lastChrome_; }

    // Load `path` into the App, retitle the window and repaint. Returns false and
    // changes NOTHING if the file will not open, which is loadDocumentFromPath's
    // contract and the reason a failed Ctrl+O leaves the document you were reading on
    // screen instead of an empty window.
    //
    // PUBLIC AND SEPARATE FROM THE DIALOG ON PURPOSE. promptOpenDocument below shows a
    // modal file dialog, so no test can call it without a human clicking; everything
    // that happens AFTER the dialog returns is in here, where a test drives it with a
    // path it wrote itself. main() takes this same path for argv[1], so the argument
    // and the menu cannot drift apart -- which is the defect main_d2d.cpp has, where
    // the load logic is two lambdas inside WinMain and the Qt side had to write its own.
    bool openDocument(const std::string& path);

    // Ctrl+O. Shows platform::requestOpenPath's dialog and hands the result to
    // openDocument. One line of untestable surface, by construction.
    void promptOpenDocument();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    // Marks the layout dirty and re-clamps the scroll offset against the new height.
    // Does NOT relayout -- see the header comment above for why that stays in the paint.
    void resizeEvent(QResizeEvent* event) override;

    // WinMain's WM_CLOSE question, asked through windowCloseMayProceed: an unsaved buffer
    // IGNORES the close and opens the unsaved-changes dialog, whose resolution posts
    // WM_CLOSE again. Every close reaches this -- the title bar, close(), and a posted
    // WM_CLOSE through the post sink.
    void closeEvent(QCloseEvent* event) override;

private:
    // Copy the widget's size into App's viewport. Returns whether it CHANGED, which is
    // what selects a relayout.
    //
    // Called from the input handlers as well as the paint, and that is not tidiness:
    // every scroll maximum is derived from app.height, so a wheel event arriving before
    // the first paint would clamp against a stale height -- 0 on a window that has
    // never painted -- and scroll nowhere. The bug it prevents is "the first scroll
    // after launch does nothing", which is exactly the kind of thing that gets
    // diagnosed as a scroll-step problem.
    bool syncViewport();

    // Relayouts if App's viewport disagrees with the widget, then draws the cached
    // layout through the seam. Returns false if there is nothing to draw, which is
    // what selects the placeholder.
    bool paintDocument();

    App& app_;
    int paintCount_ = 0;
    bool renderTargetInstalled_ = false;
    bool documentPainted_ = false;
    // One per timer id ever armed, children of this widget. Never erased: disarm stops.
    std::map<unsigned, QTimer*> timers_;
    int chromeCount_ = 0;
    tinta::WindowChrome lastChrome_;
};

#endif // TINTA_QT_WINDOW_H

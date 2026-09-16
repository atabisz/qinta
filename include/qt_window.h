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
// THE ONE THING NOT PORTED IS MATH. src/math_render.cpp is still Windows-only, so
// qinta links src/math_stub.cpp in its place and equations render as their own
// LaTeX source -- the tree's own designed fallback, and that file's header comment is
// the full account.
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
// WHAT INPUT THIS WINDOW HANDLES, and the ceiling on it: scrolling and open. That is
// two of the several hundred behaviours src/input.cpp implements -- no selection, no
// search, no tabs, no editor, no zoom. The scroll POLICY is not here either; it is
// src/scroll.cpp, which links off Windows, so this class holds the Qt-to-policy
// translation and nothing a non-Qt test cannot reach. The one thing that MUST live here
// is setFocusPolicy: a QWidget defaults to Qt::NoFocus and therefore never receives a
// key event at all, which no pixel test and no probe in this tree could have caught --
// keyPressEvent is reachable from a test with sendEvent whether the widget can be
// focused or not, so the test asserts the focus policy separately and on purpose.

#include <QWidget>
#include <string>

#include "geometry.h"

class QKeyEvent;
class QResizeEvent;
class QWheelEvent;
struct App;

class QtAppWindow final : public QWidget {
public:
    // The App is BORROWED and must outlive the window. That is main()'s job and the
    // test's, and it is the same contract QtRenderTarget has with its paint device.
    explicit QtAppWindow(App& app, QWidget* parent = nullptr);

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

    // Marks the layout dirty and re-clamps the scroll offset against the new height.
    // Does NOT relayout -- see the header comment above for why that stays in the paint.
    void resizeEvent(QResizeEvent* event) override;

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
};

#endif // TINTA_QT_WINDOW_H

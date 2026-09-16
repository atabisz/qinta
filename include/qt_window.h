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
// tinta_qt links src/math_stub.cpp in its place and equations render as their own
// LaTeX source -- the tree's own designed fallback, and that file's header comment is
// the full account.
//
// WHY THE WINDOW LAYS OUT INSIDE paintEvent, which a shipping app should not do:
// there is no resize path on this stack yet. Windows relayouts on WM_SIZE. Until the
// Qt side has its own event wiring, the paint is the only place that knows the widget
// changed size, and a window that paints a stale layout after a resize would look
// like a broken renderer. Stated here because it is a known temporary and not a
// design.

#include <QWidget>

#include "geometry.h"

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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
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

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
// WHAT IT DRAWS TODAY, said plainly so the class is not read as more than it is: the
// theme background and one line of placeholder text. It does NOT draw a document,
// and it cannot yet -- src/render.cpp does not compile off Windows. What stands
// between this window and a document is a measured list, not a guess: on 2026-09-16
// a per-file syntax-only census of every src/*.cpp outside
// tests/portable-sources.txt put 23 of 24 files in the way, on 12 distinct first
// causes. See README.md for the list.
//
// So this is the SHELL of the Qt frontend, and the two things it proves are that
// App's seam storage can hold a Qt backend and that a real Qt paint event can drive
// the abstract render target every drawing site in the tree reads.

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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    App& app_;
    int paintCount_ = 0;
    bool renderTargetInstalled_ = false;
};

#endif // TINTA_QT_WINDOW_H

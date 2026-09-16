#ifndef TINTA_QT_INIT_H
#define TINTA_QT_INIT_H

// The Qt half of App's backend installation: the two calls that put a Qt
// implementation into App's seam storage.
//
// This is the counterpart to what src/d2d_init.cpp does with seat<D2DRenderTarget>
// and seat<D2DTextFactory>, and it is a separate file for the same reason
// render_qt.cpp is separate from render_d2d.cpp: nothing off Windows can produce a
// native COM pointer, so seat() is unreachable there and Seated<>::install() is the
// only door. That door had ZERO callers until this file existed -- include/seated.h
// said so in its own comment, and a template member with no callers had never been
// instantiated, so nothing had ever type-checked it.
//
// WHY TWO LIFETIMES, WHICH IS THE ONE DESIGN DECISION HERE. The text factory is
// process-lifetime: QtTextFactory holds no state at all (see include/text_qt.h --
// QTextBoundaryFinder is constructed per call), so one instance serves the whole
// run, and the TextFormats the app caches in App must outlive any single paint.
// QtRenderTarget is the opposite: it BEGINS a QPainter in its constructor and ENDS
// it in its destructor, so its scope is exactly one paint event. Holding one past
// the end of paintEvent is a Qt warning at best and a painter drawing into a
// device that has moved on at worst. That asymmetry is why installation is one
// function and the render target is an RAII type rather than both being one call.
//
// Direct2D does not have this split -- an ID2D1HwndRenderTarget is device-lifetime
// and BeginDraw/EndDraw bracket the frame -- which is why App's storage has no
// scoping helper today and why this one lives on the Qt side rather than in app.h.

#include <memory>

class QPaintDevice;
struct App;

namespace tinta {
class RenderTarget;
}

// Installs every backend whose lifetime is the process: the text factory, the brush,
// and the sixteen cached DOCUMENT text formats. Call once, after QApplication exists
// and before the first paint.
//
// The brush joined this list when the window started drawing documents -- the paint
// pass reads App::brush at every drawing site -- and it belongs on the
// process-lifetime side because QtBrush holds a colour and no device.
//
// THE FORMATS ARE THE THIRD THING AND THEY WERE FOUND BY A CRASH, which is worth
// recording at the declaration rather than only at the definition. On Windows they come
// from updateTextFormats in src/d2d_init.cpp, a file with no non-Windows half, so off
// Windows App had a text factory and every one of app.textFormat,
// app.headingFormats[0..5], app.codeFormat and the bold/italic combinations null. The
// first document handed to a Qt window segfaulted in src/render.cpp:378 on the null.
// Only the DOCUMENT formats are created; the ~25 chrome formats are still null off
// Windows, and src/qt_init.cpp lists them.
//
// QApplication has to exist first. QtTextFactory itself constructs nothing Qt on
// its own, but the QFont/QTextLayout work every TextFormat it hands out performs
// needs the font database, and Qt's font database needs a QGuiApplication.
void installQtBackends(App& app);

// Undoes installQtBackends. Explicit rather than left to App's destructor because
// App's own teardown path is inside #ifdef _WIN32 (see app.h's cleanup members),
// so off Windows nothing would ever release it, and a test that installs twice
// would be measuring a leak rather than a seam.
void releaseQtBackends(App& app);

// One paint event's render target: installs a QtRenderTarget over the device,
// points App::renderTarget at it, and tears both down on scope exit.
//
// The pointer this makes non-null is the one every drawing site in the tree reads.
// That is the whole point -- the call sites do not learn which backend they got.
class QtScopedRenderTarget {
public:
    QtScopedRenderTarget(App& app, QPaintDevice& device);
    ~QtScopedRenderTarget();

    // Non-copyable and non-movable: it owns a QPainter's begin/end pair through the
    // App's storage, and two objects believing they own the same painter is the one
    // way to get Qt's "painter already active" warning out of this design.
    QtScopedRenderTarget(const QtScopedRenderTarget&) = delete;
    QtScopedRenderTarget& operator=(const QtScopedRenderTarget&) = delete;

    // What install() returned. Null would mean the storage refused the
    // implementation, which is worth asserting rather than assuming.
    tinta::RenderTarget* target() const { return target_; }

private:
    App& app_;
    tinta::RenderTarget* target_ = nullptr;
};

#endif // TINTA_QT_INIT_H

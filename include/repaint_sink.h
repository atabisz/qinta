#ifndef TINTA_REPAINT_SINK_H
#define TINTA_REPAINT_SINK_H

// WHERE A REPAINT REQUEST GOES WHEN THERE IS NO HWND.
//
// win32_api.h::requestRepaint is the tree's single repaint seam: 277 sites under
// src/ call it and not one of them spells InvalidateRect any more. On Windows it
// schedules a paint message. Off Windows its entire body was `(void)hwnd;` --
// which made all 277 requests silent no-ops, under a header whose own opening
// rule is "never silent successes". This file is the other arm.
//
// WHY A REGISTERED SINK RATHER THAN A DIRECT CALL. The seam lives in a header
// included across the whole tree; calling QWidget::update() from it would put Qt
// into every translation unit that repaints, including the ones that compile in
// the Windows build where Qt is not linked at all. A one-function indirection is
// what lets the Qt window own the Qt call while the seam stays Qt-free. It is
// also what makes the seam testable without a display, which the WSL probe needs.
//
// WHY THE SINK TAKES NO HWND -- MEASURED, NOT ASSUMED. The obvious signature is
// void(HWND), forwarding the token the caller already holds so a future sink can
// dispatch to one of several windows. That capability cannot exist: App::hwnd is
// assigned nowhere in the tree -- its one assignment, in the Windows-only D2D window
// creation, went with the retired src/main_d2d.cpp. The Qt build never assigns it, so
// the token is nullptr at every one of the 277 call sites, permanently. A void(HWND)
// sink would therefore receive nullptr every time -- a dispatch parameter that
// cannot dispatch, which is the same class of pretence as the no-op it replaces.
// When a second window exists off Windows, that slice adds the parameter and
// thereby says so, exactly as the seam's own no-rectangle argument does.
//
// HEADER-ONLY, over a function-local static. The accessor below owns the single
// slot; an inline function's static local has external linkage, so every
// translation unit shares one instance and there is no .cpp to add to
// tests/portable-sources.txt. That is a deliberate consequence rather than a
// convenience: the file has no platform arms, so there is nothing for a
// per-platform source list to say about it.
//
// SINGLE-THREADED BY CONTRACT. Every one of the 277 call sites runs on the thread
// that handles input, and QWidget::update() is only safe there. Nothing here
// locks, and nothing here should start locking without the caller that needs it:
// a mutex added now would be untested protection against a caller that does not
// exist. If a background thread ever needs a repaint, it posts to the GUI thread
// and the sink stays as it is.
//
// NOT INSTALLED IS NOT AN ERROR. A console build, a test binary and the Windows
// build all legitimately have no sink, and a request in those is genuinely
// nothing to do -- there is no window to repaint. That is the one case where
// doing nothing is honest rather than silent, and repaintSinkInstalled() is how a
// caller that cares can ask.

#include <functional>
#include <utility>

namespace tinta {

// A repaint request, already stripped of the window token for the reason above.
using RepaintSink = std::function<void()>;

// The single slot. Not part of the surface callers use -- setRepaintSink,
// clearRepaintSink, repaintSinkInstalled and deliverRepaintRequest are.
inline RepaintSink& repaintSinkSlot() {
    static RepaintSink sink;
    return sink;
}

// Install the sink. The Qt window calls this once, with a closure that calls
// QWidget::update() on itself.
inline void setRepaintSink(RepaintSink sink) {
    repaintSinkSlot() = std::move(sink);
}

// Remove the sink. A window being destroyed MUST call this: the sink captures
// that window, so a request delivered after it dies would reach freed memory.
// A test that installs a counting sink calls it too, so the next test starts
// from a known slot rather than from whatever ran before it.
inline void clearRepaintSink() {
    repaintSinkSlot() = nullptr;
}

inline bool repaintSinkInstalled() {
    return static_cast<bool>(repaintSinkSlot());
}

// Deliver a request to the sink if one is installed. This is what
// requestRepaint's off-Windows arm calls, and the only caller it should have.
inline void deliverRepaintRequest() {
    RepaintSink& sink = repaintSinkSlot();
    if (sink) sink();
}

}  // namespace tinta

#endif  // TINTA_REPAINT_SINK_H

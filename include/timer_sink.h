#ifndef TINTA_TIMER_SINK_H
#define TINTA_TIMER_SINK_H

// WHERE armTimer AND disarmTimer GO WHEN THERE IS NO HWND.
//
// win32_api.h's two timer calls are the tree's single timer seam. On Windows they are
// SetTimer and KillTimer, and the window procedure's WM_TIMER hands each id to
// input.cpp's handleTimer. Off the Win32 window stack their bodies were `(void)` casts,
// so on qinta every timer the tree armed was dropped: TIMER_ZOOM_APPLY never cleared
// app.zoomApplyPending, which is why Ctrl+wheel zoom could not be routed until this file.
// The comment above them said "The Qt path is a QTimer per id" -- a plan written where
// an arm should have been, the same shape repaint_sink.h's header records.
//
// SAME SHAPE AS repaint_sink.h, for the same reasons, and read that header for them: a
// registered closure keeps Qt out of the translation units the Windows build compiles,
// the slot is a function-local static so there is no .cpp, and nothing installed is an
// honest no-op -- a console build or a test binary has no window to deliver a tick to.
//
// TWO CALLS, NOT ONE, because SetTimer and KillTimer are two operations and the sink has
// to reproduce SetTimer's contract rather than approximate it: arming an id that is
// already armed RESTARTS it at the new interval, it keeps firing until disarmed, and
// disarming an id that is not armed does nothing. The Qt window's sink is written to
// those three rules; a sink that queued a second timer on re-arm would double every
// periodic tick.
//
// NO HWND, measured the same way as the repaint sink's: off the Win32 stack App::hwnd is
// nullptr at every call site, so the token could not tell two windows apart.
//
// SINGLE-THREADED BY CONTRACT, like the repaint sink: every arm and disarm runs on the
// input thread, and a QTimer can only be started from its own thread.

#include <functional>
#include <utility>

namespace tinta {

struct TimerSink {
    std::function<void(unsigned id, unsigned intervalMs)> arm;
    std::function<void(unsigned id)> disarm;
};

// The single slot. Not part of the surface callers use.
inline TimerSink& timerSinkSlot() {
    static TimerSink sink;
    return sink;
}

// The Qt window calls this once, with closures over its own QTimers.
inline void setTimerSink(TimerSink sink) {
    timerSinkSlot() = std::move(sink);
}

// A window being destroyed MUST call this: the closures capture it.
inline void clearTimerSink() {
    timerSinkSlot() = TimerSink{};
}

inline bool timerSinkInstalled() {
    const TimerSink& sink = timerSinkSlot();
    return static_cast<bool>(sink.arm) && static_cast<bool>(sink.disarm);
}

// What armTimer's and disarmTimer's off-Win32 arms call, and their only callers.
inline void deliverTimerArm(unsigned id, unsigned intervalMs) {
    TimerSink& sink = timerSinkSlot();
    if (sink.arm) sink.arm(id, intervalMs);
}

inline void deliverTimerDisarm(unsigned id) {
    TimerSink& sink = timerSinkSlot();
    if (sink.disarm) sink.disarm(id);
}

}  // namespace tinta

#endif  // TINTA_TIMER_SINK_H

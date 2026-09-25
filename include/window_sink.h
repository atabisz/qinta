#ifndef TINTA_WINDOW_SINK_H
#define TINTA_WINDOW_SINK_H

// WHERE A QUIT OR A ZEN-MODE REQUEST GOES WHEN THE WINDOW IS NOT AN HWND.
//
// The second sink after include/repaint_sink.h, for the two window requests src/input.cpp
// makes that are not repaints: requestQuit (win32_api.h, Escape with nothing open and the
// Q shortcut) and toggleZenMode (F11, and Escape while in zen). On the Win32 window stack
// both have their answer -- PostQuitMessage, and zen_mode_win32.h over the HWND. On the Qt
// stack the HWND those sites hold is nullptr, permanently: App::hwnd is assigned only by
// src/main_d2d.cpp's window creation. So the Qt window registers what to do instead, the
// same way it registers its repaint.
//
// WHY THE SAME SHAPE AS repaint_sink.h, AND WHY A SEPARATE FILE. Same reasons as there: the
// seam is reached from headers included tree-wide, including by targets that link no Qt,
// so the Qt call has to arrive as a closure. Separate because repaint_sink.h's comment
// makes a measured argument about one parameterless request, and the zen half here needs a
// value type -- ZenRestore -- that the repaint sink has no business depending on.
//
// ONE SLOT, NOT A SINK PER REQUEST. One window installs all three members and clears them
// together, so a half-installed sink is not a state anything should be able to reach.
// Each member is still tested separately at its delivery point, because a closure the
// window forgot to set is exactly the silent no-op these sinks exist to replace.
//
// SINGLE-THREADED BY CONTRACT, and NOT INSTALLED IS NOT AN ERROR -- both for the reasons
// repaint_sink.h gives. The difference that matters is what "not installed" RETURNS: a quit
// nobody can deliver returns false, which is what requestQuit's bool is for, and a zen enter
// nobody can deliver returns an invalid ZenRestore, which toggleZenMode already reads as
// "entering failed".

#include <functional>
#include <utility>

#include "window_placement.h"  // ZenRestore

namespace tinta {

struct WindowSink {
    // Exit the application's event loop.
    std::function<void()> quit;
    // zen_mode_qt.h's two calls, already bound to the window.
    std::function<ZenRestore()> zenEnter;
    std::function<bool(const ZenRestore&)> zenLeave;
};

// The single slot. Not part of the surface callers use.
inline WindowSink& windowSinkSlot() {
    static WindowSink sink;
    return sink;
}

inline void setWindowSink(WindowSink sink) {
    windowSinkSlot() = std::move(sink);
}

// A window being destroyed MUST call this: every member captures that window.
inline void clearWindowSink() {
    windowSinkSlot() = WindowSink{};
}

// True when a quit was delivered to somebody who can act on it.
inline bool deliverQuitRequest() {
    WindowSink& sink = windowSinkSlot();
    if (!sink.quit) return false;
    sink.quit();
    return true;
}

// The restore state on success; `valid == false` when there is no sink to enter with.
inline ZenRestore deliverZenEnter() {
    WindowSink& sink = windowSinkSlot();
    if (!sink.zenEnter) return ZenRestore{};
    return sink.zenEnter();
}

inline bool deliverZenLeave(const ZenRestore& restore) {
    WindowSink& sink = windowSinkSlot();
    if (!sink.zenLeave) return false;
    return sink.zenLeave(restore);
}

}  // namespace tinta

#endif  // TINTA_WINDOW_SINK_H

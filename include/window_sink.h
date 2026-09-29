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

#include "geometry.h"          // tinta::Color
#include "window_placement.h"  // ZenRestore

// OPAQUE, NOT MOVED. include/win32_api.h defines it -- that header includes this one, so a
// definition here would invert who owns the enum for a single sink member. A scoped enum's
// opaque declaration is a complete type (underlying int), which is all std::function needs.
enum class CursorShape;

namespace tinta {

// What the title bar shows for a theme: include/theme_apply.h's windowChromeFor computes it, so
// the two stacks cannot disagree about the colours, only about who can paint them.
struct WindowChrome {
    bool dark = false;
    Color caption{0, 0, 0, 1};  // caption and border fill
    Color text{0, 0, 0, 1};     // caption text
};

struct WindowSink {
    // Exit the application's event loop.
    std::function<void()> quit;
    // zen_mode_qt.h's two calls, already bound to the window.
    std::function<ZenRestore()> zenEnter;
    std::function<bool(const ZenRestore&)> zenLeave;
    // setCursorShape's off-Win32 arm (slice 3). Added after the other three and NOT covered
    // by their "installed together" argument in the sense that matters for tests: a window
    // that set three members and forgot this one keeps the arrow over text and links, which
    // is why it is asserted on its own.
    std::function<void(CursorShape)> cursor;
    // applyWindowChrome's Qt arm (slice 13). Asserted on its own for the cursor's reason: a
    // window that forgot it keeps the platform's title bar under every theme, and nothing
    // else about the window looks wrong.
    std::function<void(const WindowChrome&)> chrome;
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

// No return: SetCursor's own return (the previous cursor) is dropped by every caller too.
inline void deliverCursorShape(CursorShape shape) {
    WindowSink& sink = windowSinkSlot();
    if (sink.cursor) sink.cursor(shape);
}

// No window yet (applySavedTheme runs before one exists) is not a failure: the window applies
// the current chrome itself when it installs the sink.
inline void deliverWindowChrome(const WindowChrome& chrome) {
    WindowSink& sink = windowSinkSlot();
    if (sink.chrome) sink.chrome(chrome);
}

}  // namespace tinta

#endif  // TINTA_WINDOW_SINK_H

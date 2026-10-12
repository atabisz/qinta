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

#include "tab_ghost.h"  // TabGhostCard, for the tear-off ghost members

#include <string>
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
    // setWindowTitle's off-Win32 arm (2026-10-12), and it arrives with the same argument the
    // cursor and chrome members carry: a window that forgot this one shows whatever caption it
    // was constructed with, for the whole session, while every other thing about it is right.
    //
    // THAT WAS THE STATE UNTIL THIS MEMBER EXISTED. win32_api.h's setWindowTitle had a
    // `(void)title;` body, so all 13 updateWindowTitle call sites -- tab switch, save, new
    // note, start page, draft recovery -- computed a caption and dropped it. Two Qt files had
    // written their own QWidget::setWindowTitle calls to work around it, in the opposite word
    // order and with a full path where this one puts a file name; both are gone.
    //
    // const std::wstring& rather than const wchar_t*: the seam's own argument for the pointer
    // is that it keeps <string> out of win32_api.h, and this header already includes <string>
    // for ghostShow. The closure outlives the call, so a pointer would need a lifetime note
    // the reference does not.
    std::function<void(const std::wstring&)> title;
    // releaseMouseIfCaptured's off-Win32 arm (2026-10-12). THE CLOSURE CARRIES THE GUARD,
    // not just the release, because the guard is half the call: ReleaseCapture() lets go of
    // whatever window on the calling thread holds the mouse, so all 6 sites in the tree
    // spelled `if (GetCapture() == hwnd)` by hand. The Qt translation is
    // `QWidget::mouseGrabber() == this`, and it is what makes routing every site through one
    // seam safe instead of special-casing the one that needs it: at the five bracketed sites
    // the grab is ending anyway, and at none of them may this steal a sibling's.
    //
    // Returns whether a grab was actually released, which is the test's discriminator -- "no
    // grab now" and "no grab ever" are the same reading otherwise.
    std::function<bool()> releaseMouse;
    // The tab strip's always-on-top pin (slice 22). Returns whether the window applied it, so
    // the pin flips App::alwaysOnTop only for a request that landed -- a pin drawn as set on a
    // window that is not on top is the wrong picture the Win32 branch never shows.
    std::function<bool(bool)> stayOnTop;
    // Tab tear-off (slice 24). tinta's tab drag reads screen coordinates through ClientToScreen
    // and paints its ghost card in a layered HWND; the Qt window answers the same three
    // questions here: where a client point is on the screen, whether a screen point is on THIS
    // window, and show/move/hide of the ghost card (src/tab_ghost_qt.cpp draws it).
    std::function<void(int x, int y, int& screenX, int& screenY)> toScreen;
    std::function<bool(int screenX, int screenY)> containsScreen;
    std::function<void(const TabGhostCard& card, const std::wstring& title, int screenX,
                       int screenY)>
        ghostShow;
    std::function<void(int screenX, int screenY)> ghostMove;
    std::function<void()> ghostHide;
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

// True when a window took the always-on-top request.
inline bool deliverStayOnTop(bool on) {
    WindowSink& sink = windowSinkSlot();
    return sink.stayOnTop && sink.stayOnTop(on);
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

// Whether a window took the caption. The bool is NOT for the callers -- updateWindowTitle has
// nothing to do with a refusal and win32_api.h's setWindowTitle returns void on both arms, as
// SetWindowTextW's own return is dropped there. It is for the TEST: "the caption did not
// change" and "no window was listening" are two different states, and a void delivery leaves
// nothing to tell them apart. Same argument stayOnTop's bool makes, for a different reason.
// True when a window held the grab and let it go. False covers both "no sink" and "this
// window was not the grabber", which is correct for every caller: all six spell the
// request as "release it if it is mine", and none of them acts on a refusal.
inline bool deliverReleaseMouse() {
    WindowSink& sink = windowSinkSlot();
    return sink.releaseMouse && sink.releaseMouse();
}

inline bool deliverWindowTitle(const std::wstring& title) {
    WindowSink& sink = windowSinkSlot();
    if (!sink.title) return false;
    sink.title(title);
    return true;
}

}  // namespace tinta

#endif  // TINTA_WINDOW_SINK_H

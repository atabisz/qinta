#ifndef TINTA_WINDOW_PLACEMENT_H
#define TINTA_WINDOW_PLACEMENT_H

#include <vector>

// WHERE A TINTA WINDOW APPEARS, AS PURE ARITHMETIC OVER RECTANGLES.
//
// THE THIRD CATEGORY AGAIN, and include/win32_command_line.h is the file that named it:
// win32_types.h holds types, which can be stood in for off Windows and be genuinely
// equivalent; win32_api.h holds calls, which cannot -- "NOTHING HERE PRETENDS TO WORK".
// What lives here is neither. Deciding where a window goes is an algorithm over numbers,
// so it can be implemented EXACTLY anywhere and tested anywhere, which is the whole reason
// it is a file of its own rather than thirty-five lines inside WinMain.
//
// AND THIRTY-FIVE LINES INSIDE WinMain IS WHERE IT LIVED, WHICH IS WHY IT HAD NO TEST.
// The clamp below is the fix for issue #25 -- a window saved on a monitor that has since
// been unplugged restores off-screen -- and it has been correct by inspection for its
// entire life, because nothing in this tree can call WinMain. Shipping is not coverage.
//
// TWO RECTANGLES PER MONITOR, AND THE PAIR IS LOAD-BEARING RATHER THAN THOROUGH. The Win32
// original hit-tests with `MonitorFromRect`, which tests against the FULL monitor rect, and
// then clamps using `MONITORINFO::rcWork`, the WORK AREA. Those differ by the taskbar, so a
// window sitting entirely inside the taskbar strip is "on a monitor" to the hit test while
// being outside every work area. Collapsing the two into one rectangle would move that
// window -- a behaviour change disguised as a simplification, and one no reader would
// notice, because both spellings look like "the monitor". Qt hands over the same pair:
// QScreen::geometry() and QScreen::availableGeometry().
//
// WHAT IS NOT FAITHFUL, stated because the alternative is a parity claim that is false.
// `MONITOR_DEFAULTTONEAREST` does not document its metric, so "nearest" below is a
// DEFINITION rather than a reproduction: the monitor minimising the RECT-TO-RECT distance
// from the saved window to that monitor's bounds -- zero on every axis where the two
// overlap -- with ties broken by list order.
//
// THIS PARAGRAPH DESCRIBED A CENTRE-TO-RECT METRIC UNTIL IT WAS MEASURED, and the
// correction is worth more than the sentence it replaced. Centre-based is the obvious
// reading and it is wrong: swept against the real `MONITOR_DEFAULTTONEAREST` over 17,328
// saved positions on this three-monitor desktop, it disagreed on 288 of them, every
// disagreement in an inter-monitor seam. This desktop has a ONE-PIXEL vertical gap at
// x=3440, and a window landing in it is nearest to the monitor it is TOUCHING even though
// its centre sits deep over the other one. Win32 reads the window, so this reads the window.
// The residue after the fix is 18 cases, all of them genuine TIES at 1x1, and they are left
// alone deliberately: Win32's tie order is undocumented, and fitting it to two samples on
// one machine would produce an instrument that agrees with me rather than a reading.
//
// What remains un-faithful is only that tie order, and on a one-monitor machine the
// question does not arise.
namespace tinta {

// How far a cascaded window steps down and right. Named rather than inline so the test
// asserts the same quantity the code applies -- a test carrying its own literal 40 passes
// when the code's 40 is changed to 50, which makes it a test of nothing.
inline constexpr int kCascadeStep = 40;

// Integer pixels, deliberately, and NOT types.h's Rect. Two reasons, either sufficient:
// window geometry is integral at every OS boundary it crosses, and
// `kWindowPositionUseDefault` is the bit pattern 0x80000000 compared for EQUALITY -- a
// sentinel that survives a float round-trip only by luck is a sentinel waiting to stop
// matching. types.h's Rect is float and carries `width`/`height` for layout; this one is
// about pixels on a desktop.
struct WindowRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// One display. `bounds` is what a saved window is tested AGAINST; `work` is what it is
// clamped INTO. See the paragraph above on why these are not the same rectangle.
struct MonitorArea {
    WindowRect bounds;
    WindowRect work;
};

// WHAT ZEN MODE MUST COME BACK TO, AND DELIBERATELY NOT A `WINDOWPLACEMENT`.
//
// F11 goes borderless over one whole monitor and F11 again undoes it, so something has to
// remember the window it was. That something used to be a `WINDOWPLACEMENT` stored by value in
// App -- which compiled off Windows only because win32_types.h stood the struct in field for
// field, and which meant the state at the centre of a two-platform capability was spelled in a
// vocabulary one of the two platforms has no word for. A seam whose state type is a Win32 struct
// has not moved the coupling; it has hidden it behind a file boundary.
//
// TWO FIELDS, AND THE TREE ALREADY AGREED THIS IS THE RIGHT PAIR BEFORE THIS STRUCT EXISTED.
// src/main_d2d.cpp's save path takes a real `WINDOWPLACEMENT` off the window and immediately
// decomposes it into `settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED)` plus a
// position -- a rectangle and a flag. That is this struct, hand-written at one call site.
//
// WHAT IS LOST RELATIVE TO SetWindowPlacement, STATED RATHER THAN DISCOVERED. The SDK struct
// also carries `ptMinPosition` and `ptMaxPosition`. The first is IGNORED by SetWindowPlacement
// unless `WPF_SETMINPOSITION` is set, which the shipped code never set; the second is where a
// maximized window's top-left would go, which Windows recomputes per monitor. Neither is a
// concept Qt has, and neither is observable on the only transition zen mode can make -- F11 is
// pressed on a window somebody is looking at, so `showCmd` is normal or maximized and never
// minimized. The Win32 arm rebuilds a `WINDOWPLACEMENT` from these two fields; the divergence is
// priced here so the next reader does not have to re-derive that it is nil.
struct ZenRestore {
    WindowRect normal;       // the window's normal-position rectangle, pre-zen
    bool maximized = false;  // was it maximized when zen mode was entered
    bool valid = false;      // nothing captured yet -- leaving zen mode without having entered it
};

// Everything the decision depends on, so the function has no other inputs and no clock,
// no filesystem and no OS behind it.
struct PlacementRequest {
    // As loaded from Settings. `savedX`/`savedY` may be kWindowPositionUseDefault, which
    // means "let the window manager choose" and must survive to the caller unchanged.
    int savedX = 0;
    int savedY = 0;
    int savedWidth = 0;
    int savedHeight = 0;

    bool cascade = false;  // --cascade: step down-right so a spawned window does not cover
                           // the one that spawned it
    bool hasPos = false;   // --pos was given, and then it wins outright
    int posX = 0;
    int posY = 0;
};

// The saved size is returned unchanged except where the clamp had to shrink it to fit.
// The returned x/y are kWindowPositionUseDefault only if the request's were and nothing
// overrode them -- so a caller can hand the pair straight to CreateWindowExW, or test for
// the sentinel and let Qt choose.
WindowRect choosePlacement(const PlacementRequest& request,
                           const std::vector<MonitorArea>& monitors);

// The rectangle zen mode covers: the FULL bounds of the monitor nearest `window`.
//
// `bounds` and not `work`, which is the one thing in this function that could be got wrong
// invisibly. Zen mode is borderless fullscreen and deliberately covers the taskbar; clamping it
// into the work area instead would leave a strip of desktop at the bottom of a "fullscreen"
// window, and the shipped Win32 code reads `MONITORINFO::rcMonitor` for exactly this reason.
// Everything else in this file clamps into `work`, so this is the asymmetry to notice.
//
// NEAREST BY THE SAME METRIC choosePlacement uses -- literally the same function, not a second
// implementation that agrees today. The shipped code asked Win32 with
// `MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)`; this file's `distanceSquared` was swept
// against that API over 17,328 positions and made to match it in the inter-monitor seams, so
// reusing it is what makes the port faithful rather than merely plausible.
//
// WITH NO MONITORS the answer is `window` UNCHANGED. There is no nearest of nothing, and the
// alternatives are both worse than doing nothing: a default-constructed WindowRect would collapse
// the window to 0x0 at the origin, and the sentinel `choosePlacement` returns in the same
// situation is meaningful for "where should a new window go" and meaningless for "what should
// this window cover". A window that stays put is a visible failure to enter zen mode, which is
// the behaviour this tree prefers to a confident wrong rectangle.
WindowRect zenFullscreenRect(const WindowRect& window, const std::vector<MonitorArea>& monitors);

}  // namespace tinta

#endif  // TINTA_WINDOW_PLACEMENT_H

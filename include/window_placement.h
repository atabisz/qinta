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

}  // namespace tinta

#endif  // TINTA_WINDOW_PLACEMENT_H

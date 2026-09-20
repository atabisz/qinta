#include "window_placement.h"

#include <algorithm>
#include <cstdint>

#include "settings_data.h"

namespace tinta {
namespace {

bool intersects(const WindowRect& a, const WindowRect& b) {
    // Half-open on both axes, which is what a pixel rectangle means: a window whose right
    // edge is exactly a monitor's left edge touches it and occupies none of it. The Win32
    // original inherits this from IntersectRect, and getting it backwards here would make a
    // window one pixel off-screen count as on-screen and skip the clamp.
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

// Distance between two rectangles, squared, in int64 because a two-monitor desktop spanning
// 8K is already past what squaring an int can hold and the overflow would be silent -- it
// would pick a monitor, plausibly, and for no reason.
//
// RECT to RECT, not CENTRE to rect. That distinction is not a refinement, it is the
// difference between agreeing with the OS and not: measured against
// MONITOR_DEFAULTTONEAREST over 17,328 saved positions on a three-monitor desktop, a
// centre-based metric disagreed on 288 of them. The disagreements were all in the seams --
// this desktop has a ONE-PIXEL vertical gap at x=3440 between two monitors, and a window
// landing in it is nearest to the monitor it is TOUCHING even though its centre sits deep
// over the other one. Win32 reads the window, so this does too.
int64_t distanceSquared(const WindowRect& a, const WindowRect& b) {
    const int64_t dx =
        std::max(0, std::max(a.x - (b.x + b.width), b.x - (a.x + a.width)));
    const int64_t dy =
        std::max(0, std::max(a.y - (b.y + b.height), b.y - (a.y + a.height)));
    return dx * dx + dy * dy;
}

}  // namespace

WindowRect choosePlacement(const PlacementRequest& request,
                           const std::vector<MonitorArea>& monitors) {
    WindowRect out;
    out.x = request.savedX;
    out.y = request.savedY;
    out.width = request.savedWidth;
    out.height = request.savedHeight;

    // A saved position of "let the window manager choose" is not a position, so there is
    // nothing to validate: no monitor contains it and clamping it would invent one.
    const bool savedIsReal = out.x != kWindowPositionUseDefault &&
                             out.y != kWindowPositionUseDefault;

    if (savedIsReal) {
        const WindowRect saved = out;
        const bool onSomeMonitor =
            std::any_of(monitors.begin(), monitors.end(),
                        [&](const MonitorArea& m) { return intersects(saved, m.bounds); });

        if (!onSomeMonitor) {
            // Fully off every live display: bring it back into the nearest one's work
            // area. WITH NO MONITORS AT ALL there is no nearest and no work area, so the
            // answer is the sentinel -- "let the window manager choose" is the only honest
            // response to "place this among zero displays", and it is also what the Win32
            // original does when GetMonitorInfoW fails.
            const MonitorArea* nearest = nullptr;
            int64_t best = 0;
            for (const MonitorArea& m : monitors) {
                const int64_t d = distanceSquared(saved, m.bounds);
                if (!nearest || d < best) {
                    nearest = &m;
                    best = d;
                }
            }

            if (nearest) {
                const WindowRect& wa = nearest->work;
                out.width = std::min(saved.width, wa.width);
                out.height = std::min(saved.height, wa.height);
                out.x = std::max(wa.x, std::min(saved.x, wa.x + wa.width - out.width));
                out.y = std::max(wa.y, std::min(saved.y, wa.y + wa.height - out.height));
            } else {
                out.x = kWindowPositionUseDefault;
                out.y = kWindowPositionUseDefault;
            }
        }
    }

    // --pos wins outright: a tab dragged out appears under the drop point, and no saved
    // position, cascade step or clamp may move it. Deliberately NOT validated against the
    // monitors -- the drop point came from a live cursor, so it is on a display by
    // construction, and clamping it would fight the gesture that produced it.
    if (request.hasPos) {
        out.x = request.posX;
        out.y = request.posY;
        return out;
    }

    // Cascade steps down-right from the saved position so a spawned window does not cover
    // the window that spawned it. Guarded on the sentinel: offsetting "let the window
    // manager choose" by 40 turns a sentinel into a POSITION, 40 pixels from the top-left
    // of a coordinate space whose origin is not necessarily a corner of anything.
    if (request.cascade && out.x != kWindowPositionUseDefault &&
        out.y != kWindowPositionUseDefault) {
        out.x += kCascadeStep;
        out.y += kCascadeStep;
    }

    return out;
}

}  // namespace tinta

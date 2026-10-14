// THE FIRST TEST THE #25 OFF-SCREEN CLAMP HAS EVER HAD.
//
// The logic it exercises shipped inside WinMain (retired in 0b39ae6), where nothing in this tree could reach it, so
// for its whole life its evidence was that it looked right and nobody complained. That is
// not the same as a passing test, and the difference is the reason this file exists rather
// than the clamp being moved quietly.
//
// EVERY CASE HERE STATES WHAT WOULD BE TRUE IF THE FUNCTION DID NOTHING, because a placement
// function is unusually easy to satisfy by accident: it returns a rectangle, most inputs are
// already valid, and "give back what you were given" passes any assertion that only checks
// the ordinary case. So the cases that matter are the ones where pass-through is WRONG, and
// they are marked.
#include "window_placement.h"

#include <cstdio>

#include "settings_data.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

tinta::WindowRect rect(int x, int y, int w, int h) {
    tinta::WindowRect r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

// A plain 1920x1080 display with a 40-pixel taskbar along the bottom, so `bounds` and
// `work` genuinely differ and a test that confused them would read differently.
tinta::MonitorArea primary() {
    tinta::MonitorArea m;
    m.bounds = rect(0, 0, 1920, 1080);
    m.work = rect(0, 0, 1920, 1040);
    return m;
}

// A second display to the right, as a docked laptop has, at a different size so the clamp's
// choice of monitor is observable in the result rather than only in the intent.
tinta::MonitorArea secondary() {
    tinta::MonitorArea m;
    m.bounds = rect(1920, 0, 1280, 1024);
    m.work = rect(1920, 0, 1280, 984);
    return m;
}

bool contains(const tinta::WindowRect& outer, const tinta::WindowRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

tinta::PlacementRequest saved(int x, int y, int w, int h) {
    tinta::PlacementRequest r;
    r.savedX = x;
    r.savedY = y;
    r.savedWidth = w;
    r.savedHeight = h;
    return r;
}

}  // namespace

int main() {
    // --- CONTROL: a position that is fine is LEFT ALONE ----------------------
    //
    // Without this, every assertion below is satisfiable by a function that clamps
    // unconditionally -- which would "fix" #25 by moving every window on every launch, a
    // regression that no off-screen test can see.
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(100, 120, 1024, 768), {primary()});
        check(out.x == 100 && out.y == 120 && out.width == 1024 && out.height == 768,
              "CONTROL: a saved position already on a monitor is returned untouched -- a "
              "function that clamped unconditionally would pass every off-screen case below");
    }

    // --- #25: a window saved on a monitor that is now gone -------------------
    //
    // PASS-THROUGH IS WRONG HERE, and this is the case the issue was filed about: the
    // external monitor was at x=1920, the laptop is now undocked, and the saved rect sits
    // in coordinates no display covers.
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(2400, 200, 1024, 768), {primary()});
        check(contains(primary().work, out),
              "#25: a rect saved on a since-disconnected monitor is clamped INTO the "
              "remaining display's work area -- it was at x=2400 with nothing there");
        check(out.x != 2400,
              "#25: and the x it came in with is not the x it goes out with, which is the "
              "part a containment check alone would not catch if the work area were huge");
        std::printf("       #25 clamp: 2400,200 1024x768 -> %d,%d %dx%d\n", out.x, out.y,
                    out.width, out.height);
    }

    // --- The clamp lands in the WORK area, not the monitor bounds -------------
    //
    // A window clamped to the bottom of the monitor rather than the bottom of the work area
    // sits under the taskbar. The two differ by 40 pixels here, so the assertion can tell.
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(-5000, 5000, 1024, 768), {primary()});
        check(out.y + out.height <= primary().work.y + primary().work.height,
              "the clamp respects the WORK area's bottom edge, not the monitor's -- 40 "
              "pixels of taskbar separate the two and a window under it cannot be moved");
    }

    // --- Taskbar-only: on a monitor, outside every work area -----------------
    //
    // THE CASE THAT MAKES THE TWO RECTANGLES LOAD-BEARING. This rect intersects the monitor
    // bounds and misses the work area entirely. The Win32 original hit-tests bounds, so it
    // leaves this window alone; a version that hit-tested work areas would move it. The
    // assertion is that we match the original.
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(200, 1050, 400, 20), {primary()});
        check(out.x == 200 && out.y == 1050,
              "a window inside the taskbar strip is ON a monitor and is NOT clamped -- the "
              "hit test is against the monitor bounds, which is why MonitorArea carries two "
              "rectangles rather than one");
    }

    // --- A rect larger than the work area it is clamped into ------------------
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(-9000, -9000, 4000, 3000), {primary()});
        check(out.width <= primary().work.width && out.height <= primary().work.height,
              "a saved size larger than the work area is SHRUNK to fit rather than clamped "
              "to a position that cannot contain it");
        check(contains(primary().work, out),
              "and the shrunk rect is inside the work area, which is the reading that would "
              "fail if only the position had been clamped");
    }

    // --- Nearest of several, and the choice is observable ---------------------
    {
        const tinta::WindowRect nearSecond =
            tinta::choosePlacement(saved(3400, 200, 800, 600), {primary(), secondary()});
        check(contains(secondary().work, nearSecond),
              "a rect off the right-hand edge of a two-monitor desktop comes back into the "
              "RIGHT-HAND monitor, not the first one in the list");
        const tinta::WindowRect nearFirst =
            tinta::choosePlacement(saved(-3000, 200, 800, 600), {primary(), secondary()});
        check(contains(primary().work, nearFirst),
              "COUNTER-CASE: and a rect off the LEFT edge comes back into the left-hand "
              "monitor -- without this pair, 'nearest' is indistinguishable from 'the first "
              "one' or 'the last one'");
    }

    // --- "Nearest" is measured from the WINDOW, not from its centre -----------
    //
    // THIS CASE WAS WRITTEN BY A DISAGREEMENT WITH THE OPERATING SYSTEM, not by inspection.
    // The seam originally measured centre-of-window to monitor, and against
    // MONITOR_DEFAULTTONEAREST over 17,328 saved positions on a real three-monitor desktop it
    // disagreed on 288 of them. Every disagreement was in a seam between displays: this
    // machine has a ONE-PIXEL vertical gap at x=3440, and a window landing in it is touching
    // one monitor while its centre sits deep over another. Win32 measures the window, so the
    // seam does too.
    //
    // None of the monitor pairs above can tell the two metrics apart, because they are
    // edge-adjacent and the answers coincide. This pair has a two-pixel gap, which is the
    // whole point of it: under a centre-based metric the window below lands in the RIGHT-hand
    // display and this assertion fails.
    {
        tinta::MonitorArea gapLeft;
        gapLeft.bounds = rect(0, 0, 1000, 1000);
        gapLeft.work = rect(0, 0, 1000, 960);
        tinta::MonitorArea gapRight;
        gapRight.bounds = rect(1002, 0, 500, 400);
        gapRight.work = rect(1002, 0, 500, 380);

        const tinta::WindowRect out =
            tinta::choosePlacement(saved(1001, 600, 800, 200), {gapLeft, gapRight});
        check(contains(gapLeft.work, out),
              "a window in the GAP between two displays returns to the one it is touching, "
              "not the one its centre hovers over -- the centre-based metric this replaced "
              "disagreed with MONITOR_DEFAULTTONEAREST on 288 of 17,328 real cases");
        check(out.x == 200 && out.y == 600,
              "and the clamped position is exact, so the case cannot pass by landing in the "
              "right work area for the wrong reason");
    }

    // --- ISC-7: zero monitors is DECIDABLE -----------------------------------
    //
    // A headless session, a remote desktop mid-reconnect, or a Qt arm asked before any
    // screen is attached. The answer is stated rather than left to whatever the loop does
    // when it never runs.
    {
        const tinta::WindowRect out =
            tinta::choosePlacement(saved(100, 120, 1024, 768), {});
        check(out.x == kWindowPositionUseDefault && out.y == kWindowPositionUseDefault,
              "with NO monitors the position degrades to the use-default sentinel -- there "
              "is no nearest display to clamp into, and inventing one would place a window "
              "in a coordinate space that does not exist");
        check(out.width == 1024 && out.height == 768,
              "and the SIZE survives, because nothing about an absent display makes a "
              "remembered size wrong");
    }

    // --- ISC-8: the sentinel survives, and cascade does not offset it ---------
    //
    // PASS-THROUGH IS RIGHT HERE and the failure mode is the opposite one: a cascade applied
    // to 0x80000000 produces 0x80000028, which is not a sentinel and not a sane position --
    // it is a number 40 past the most negative int, silently.
    {
        tinta::PlacementRequest r =
            saved(kWindowPositionUseDefault, kWindowPositionUseDefault, 1024, 768);
        r.cascade = true;
        const tinta::WindowRect out = tinta::choosePlacement(r, {primary()});
        check(out.x == kWindowPositionUseDefault && out.y == kWindowPositionUseDefault,
              "--cascade on an unset saved position leaves the sentinel ALONE -- offsetting "
              "it by 40 turns 'let the window manager choose' into a position");
    }

    // --- Cascade on a real position, against the named constant --------------
    {
        tinta::PlacementRequest r = saved(300, 400, 1024, 768);
        r.cascade = true;
        const tinta::WindowRect out = tinta::choosePlacement(r, {primary()});
        check(out.x == 300 + tinta::kCascadeStep && out.y == 400 + tinta::kCascadeStep,
              "--cascade steps down-right by kCascadeStep -- asserted against the constant "
              "the code applies, so changing the step cannot leave this test green");
    }

    // --- --pos wins outright --------------------------------------------------
    {
        tinta::PlacementRequest r = saved(300, 400, 1024, 768);
        r.cascade = true;
        r.hasPos = true;
        r.posX = 777;
        r.posY = 888;
        const tinta::WindowRect out = tinta::choosePlacement(r, {primary()});
        check(out.x == 777 && out.y == 888,
              "--pos beats both the saved position and --cascade -- the two flags arrive "
              "TOGETHER from tabDragEnd, so an implementation that added the cascade step to "
              "the drop point would be wrong on the only path that sends both");
    }

    // --- --pos is deliberately NOT validated ---------------------------------
    {
        tinta::PlacementRequest r = saved(300, 400, 1024, 768);
        r.hasPos = true;
        r.posX = -4000;
        r.posY = -4000;
        const tinta::WindowRect out = tinta::choosePlacement(r, {primary()});
        check(out.x == -4000 && out.y == -4000,
              "an off-screen --pos is honoured rather than clamped -- documented in the "
              "header as deliberate, and asserted here so a later clamp-everything change "
              "has to come past this line");
    }

    std::printf("%s%d window-placement assertions run, %d failed\n",
                failures == 0 ? "all " : "", checks, failures);
    return failures == 0 ? 0 : 1;
}

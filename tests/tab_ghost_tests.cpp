// THE DRAG GHOST'S PORTABLE DECISIONS, ASSERTED WITHOUT A WINDOW.
//
// Everything in this file was, until this slice, unreachable by any test. The numbers lived inside
// a Win32-only function that read `App&`, so exercising them meant standing up a real window, a
// theme, a document and a message loop -- and nobody did, which is why a card whose corners were
// asymmetric would have shipped unnoticed.
//
// TWO ASSERTIONS HERE ARE NOT REGRESSION TESTS, they are new claims about the shipped behaviour:
// the corner symmetry (which the shipped loop never asserted and is not obvious from reading it,
// because the left branches measure from `radius` and the right ones from `width - 1 - radius`),
// and the truncation at fractional scale. Every other number is the shipped one, and where a number
// looks arbitrary it is arbitrary in the shipping app too -- reproducing it exactly is the point,
// since a changed constant would be indistinguishable from a porting mistake.
#include "tab_ghost.h"

#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

}  // namespace

int main() {
    // --- ISC-2: the shipped numbers, at scale 1.0 ----------------------------
    {
        const tinta::TabGhostGeometry g = tinta::tabGhostGeometry(1.0f);
        check(g.width == 190, "GEOMETRY: the card is 190 device pixels wide at scale 1.0");
        check(g.height == 34, "GEOMETRY: and 34 tall");
        check(g.radius == 8.0f, "GEOMETRY: with an 8 pixel corner radius");
        check(g.textLeftInset == 12, "GEOMETRY: the title starts 12 pixels in");
        check(g.textRightInset == 10, "GEOMETRY: and is ellipsised 10 pixels from the right edge");
        check(g.fontHeight == 13.0f,
              "GEOMETRY: the title is 13 pixels tall -- as a MAGNITUDE; the Win32 arm negates it "
              "for CreateFontW, and a seam that stored the negative would hand Qt a nonsense size");
        // The inset pair is only meaningful as a pair: it has to leave room for text. A geometry
        // whose insets overlapped would draw an empty card and no assertion above would notice.
        check(g.width - g.textLeftInset - g.textRightInset == 168,
              "GEOMETRY: which leaves 168 pixels of title, so the insets do not meet");
    }

    // --- The scale is a multiplier, and it TRUNCATES -------------------------
    {
        // 1.25 is the scale of a 125% Windows display, the commonest non-integral case in the
        // fleet, and every value below is exactly representable in binary -- so this asserts the
        // truncation rule, not a float coincidence. 190 * 1.25 is 237.5, and the shipped code says
        // `(int)dpi(app, 190.0f)`, which is 237. A refactor that rounded would move the card's
        // right edge a pixel on every 125% display at once.
        const tinta::TabGhostGeometry g = tinta::tabGhostGeometry(1.25f);
        check(g.width == 237, "SCALE: 190 at 1.25 truncates to 237, not 238");
        check(g.height == 42, "SCALE: 34 at 1.25 truncates to 42, not 43");
        check(g.radius == 10.0f,
              "SCALE: but the RADIUS is not truncated -- the shipped code keeps it a float and "
              "squares it in the coverage test, so truncating would change which pixels the "
              "corners cover");
        const tinta::TabGhostGeometry doubled = tinta::tabGhostGeometry(2.0f);
        check(doubled.width == 380 && doubled.height == 68 && doubled.radius == 16.0f,
              "SCALE: and at 2.0 everything simply doubles");
        // CONTROL: the geometry must actually depend on its argument. An implementation that
        // ignored `scale` and returned the constants would satisfy every assertion in the first
        // block above.
        check(tinta::tabGhostGeometry(1.0f).width != doubled.width,
              "SCALE CONTROL: the geometry changes with scale, so the block above is reading a "
              "function and not a table of constants");
    }

    // --- ISC-3: the coverage rule --------------------------------------------
    {
        const tinta::TabGhostGeometry g = tinta::tabGhostGeometry(1.0f);
        check(tinta::tabGhostCovers(g, 95, 17),
              "COVERAGE: the middle of the card is covered");
        check(!tinta::tabGhostCovers(g, 0, 0),
              "COVERAGE: the top-left pixel is NOT -- the corners are rounded");
        check(!tinta::tabGhostCovers(g, 189, 33),
              "COVERAGE: nor the bottom-right pixel");
        check(tinta::tabGhostCovers(g, 0, 17),
              "COVERAGE: but the left edge at mid-height is, so this is a rounded rectangle and "
              "not four arcs -- the whole body between the corner squares belongs to the card");
        check(tinta::tabGhostCovers(g, 95, 0) && tinta::tabGhostCovers(g, 95, 33),
              "COVERAGE: and so are the top and bottom edges at mid-width");
        // The DISCRIMINATOR for the whole predicate: with no radius there are no corners to cut,
        // so every pixel including (0,0) is covered. If this passed while the corner assertions
        // above also passed on a predicate that ignored `radius`, one of the two would be wrong.
        tinta::TabGhostGeometry square = g;
        square.radius = 0.0f;
        check(tinta::tabGhostCovers(square, 0, 0) && tinta::tabGhostCovers(square, 189, 33),
              "COVERAGE DISCRIMINATOR: at radius 0 the corners are covered too, so the predicate "
              "reads the radius rather than hardcoding a corner cut");
        // And a radius large enough to meet in the middle must still leave the centre covered:
        // the four corner squares tile the whole card at radius = height/2, so a sign error in the
        // distance test would empty it.
        tinta::TabGhostGeometry round = g;
        round.radius = 17.0f;
        check(tinta::tabGhostCovers(round, 95, 17),
              "COVERAGE: at a radius that meets in the middle the centre is still covered");
    }

    // --- ISC-4: the four corners are symmetric ------------------------------
    {
        // THE ASSERTION THE SHIPPED LOOP NEVER HAD. Its four branches are not visibly mirror
        // images of each other -- the left-hand pair measures from `radius`, the right-hand pair
        // from `width - 1 - radius` -- so a one-pixel error in either right-hand branch would
        // produce a card with a heavier corner on one side, which is exactly the kind of thing that
        // looks like a font-rendering artefact and never gets filed.
        //
        // Every pixel, all four reflections, one count. Whole-surface rather than sampled because
        // a sampled version would miss a single-pixel asymmetry, which is the only kind this
        // arithmetic can produce.
        const tinta::TabGhostGeometry g = tinta::tabGhostGeometry(1.0f);
        int mismatches = 0;
        int covered = 0;
        for (int py = 0; py < g.height; py++) {
            for (int px = 0; px < g.width; px++) {
                const bool here = tinta::tabGhostCovers(g, px, py);
                if (here) covered++;
                if (here != tinta::tabGhostCovers(g, g.width - 1 - px, py)) mismatches++;
                if (here != tinta::tabGhostCovers(g, px, g.height - 1 - py)) mismatches++;
                if (here != tinta::tabGhostCovers(g, g.width - 1 - px, g.height - 1 - py)) {
                    mismatches++;
                }
            }
        }
        check(mismatches == 0,
              "SYMMETRY: the card is identical under horizontal, vertical and 180-degree "
              "reflection -- all four corners are cut the same way");
        // A symmetry assertion passes trivially on a predicate that returns the same answer
        // everywhere, so the count is asserted too: the corners must actually remove pixels, and
        // they must not remove many.
        check(covered > 0 && covered < g.width * g.height,
              "SYMMETRY CONTROL: some pixels are covered and some are not, so the symmetry above "
              "is a property of a real shape and not of a constant");
        const int cut = g.width * g.height - covered;
        check(cut > 4 && cut < 100,
              "SYMMETRY CONTROL: and the corners remove a corner's worth of pixels rather than a "
              "handful or a quadrant");
        check(cut % 4 == 0,
              "SYMMETRY: the cut count divides by four, which it cannot if one corner differs "
              "from the others by a single pixel");
    }

    // --- ISC-2, second half: the placement arithmetic ------------------------
    {
        // The card is positioned relative to the cursor, and these three numbers were at the CALL
        // SITE rather than in the ghost's own code -- which is why they read as incidental and why
        // nothing tested them. They are the difference between a card being carried and a card
        // hanging off the pointer.
        //
        // The clamp is only observable through a grab WIDER than it, and the floor only through a
        // grab narrower, so each needs its own case.
        const tinta::TabGhostPlacement wide = tinta::tabGhostPlacement(1.0f, 1000, 500, 900);
        check(wide.x == 1000 - 166,
              "PLACEMENT: a grab 900 pixels into a wide tab is clamped to 166, so the cursor stays "
              "on the card instead of ending up off its left edge");
        const tinta::TabGhostPlacement edge = tinta::tabGhostPlacement(1.0f, 1000, 500, 0);
        check(edge.x == 1000 - 12,
              "PLACEMENT: a grab at the very left edge is floored at 12, so the cursor does not "
              "sit on the card's own border");
        const tinta::TabGhostPlacement middle = tinta::tabGhostPlacement(1.0f, 1000, 500, 80);
        check(middle.x == 1000 - 80,
              "PLACEMENT: and a grab between the two is honoured exactly -- the clamps are limits, "
              "not a fixed offset");
        check(middle.y == 500 - 17,
              "PLACEMENT: the card is lifted 17 pixels above the pointer, so the cursor does not "
              "cover the start of the title");
        check(wide.y == middle.y && edge.y == middle.y,
              "PLACEMENT: the lift does not depend on the horizontal grab");
        // Both clamps scale. A placement that scaled the cursor but not the clamps would drift.
        const tinta::TabGhostPlacement scaled = tinta::tabGhostPlacement(1.25f, 1000, 500, 900);
        check(scaled.x == 1000 - 207 && scaled.y == 500 - 21,
              "PLACEMENT: at 1.25 the clamp is 207 and the lift 21, both truncated like the "
              "geometry");
        const tinta::TabGhostPlacement scaledEdge = tinta::tabGhostPlacement(1.25f, 1000, 500, 0);
        check(scaledEdge.x == 1000 - 15, "PLACEMENT: and the floor scales to 15");
    }

    // --- The detach threshold ----------------------------------------------
    {
        // WHEN the card exists. `y` is the pointer in client coordinates, so it goes negative above
        // the window, and the band is 48 either side of the strip. Both boundaries are asserted on
        // the exact pixel, because a `>=` where the shipped code has `>` is the single most likely
        // way for this to be ported wrong and the least visible.
        const float strip = 40.0f;
        check(!tinta::tabGhostShouldDetach(1.0f, 0.0f, strip),
              "DETACH: a pointer at the top of the strip does not detach");
        check(!tinta::tabGhostShouldDetach(1.0f, 88.0f, strip),
              "DETACH: nor one exactly 48 below the strip -- the test is strictly greater");
        check(tinta::tabGhostShouldDetach(1.0f, 88.5f, strip),
              "DETACH: one pixel further down does");
        check(!tinta::tabGhostShouldDetach(1.0f, -48.0f, strip),
              "DETACH: exactly 48 ABOVE the window does not detach either");
        check(tinta::tabGhostShouldDetach(1.0f, -48.5f, strip),
              "DETACH: and just past that does, which is how a tab dragged up off the title bar "
              "becomes a window");
        check(tinta::tabGhostShouldDetach(1.0f, 200.0f, strip),
              "DETACH: well down into the document detaches");
        // The band scales and the strip does not: stripHeight arrives already in device pixels
        // from chromeTopHeight, so scaling it here would double-apply the scale.
        check(!tinta::tabGhostShouldDetach(2.0f, 130.0f, strip) &&
                  tinta::tabGhostShouldDetach(1.0f, 130.0f, strip),
              "DETACH: the 48 band scales but stripHeight does not -- it arrives already scaled, "
              "and scaling it again would double-apply contentScale");
        // A taller strip moves the lower boundary with it, which is the only reason it is a
        // parameter rather than a constant.
        check(!tinta::tabGhostShouldDetach(1.0f, 130.0f, 90.0f),
              "DETACH: a taller strip pushes the lower boundary down with it");
    }

    // --- The card carries colours through untouched -------------------------
    {
        const tinta::Color fill{0.1f, 0.2f, 0.3f, 1.0f};
        const tinta::Color border{0.9f, 0.8f, 0.7f, 1.0f};
        const tinta::Color text{0.5f, 0.5f, 0.5f, 1.0f};
        const tinta::TabGhostCard card = tinta::tabGhostCard(1.0f, fill, border, text);
        check(card.geometry.width == 190 && card.geometry.height == 34,
              "CARD: the card carries the geometry for its scale");
        check(card.fill.r == fill.r && card.fill.g == fill.g && card.fill.b == fill.b,
              "CARD: and the fill colour it was handed, unmodified -- no premultiplication, no "
              "gamma, because both arms do their own conversion and doing it twice is invisible "
              "until somebody compares screenshots");
        check(card.border.r == border.r && card.text.r == text.r,
              "CARD: border and text are distinct fields even though the app passes the same "
              "colour for both today, because a Qt arm drawing the border in the text colour by "
              "coincidence would be a bug the day the theme separates them");
        check(tinta::kTabGhostAlpha == 235,
              "CARD: and the whole-card opacity is the shipped 235/255, in the header rather than "
              "in either arm -- a card 92% opaque on Windows and 100% on Qt is the drift this "
              "seam exists to prevent");
    }

    std::printf("%s: %d/%d assertions passed\n", failures == 0 ? "PASS" : "FAIL",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

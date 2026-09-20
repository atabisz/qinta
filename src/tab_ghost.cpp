#include "tab_ghost.h"

#include <algorithm>

namespace tinta {
namespace {

// `dpi(app, v)` is `v * app.contentScale` (include/app.h). Every number below came out of a
// `(int)dpi(app, N)` at the Win32 call site, so the truncation is reproduced rather than rounded:
// at scale 1.25 the shipped card is 237 pixels wide, not 238, and a refactor that quietly rounded
// would move the card by a pixel on every non-integral display in the fleet.
int scaled(float scale, float value) {
    return static_cast<int>(value * scale);
}

}  // namespace

TabGhostGeometry tabGhostGeometry(float scale) {
    TabGhostGeometry geometry;
    geometry.width = scaled(scale, 190.0f);
    geometry.height = scaled(scale, 34.0f);
    // NOT truncated, and the asymmetry is deliberate: the shipped code holds the radius as
    // `float radius = dpi(app, 8.0f)` and squares it in the coverage test, so truncating here would
    // change which pixels the corners cover at fractional scales. It is the one number the Win32
    // arm kept as a float, and it stays one.
    geometry.radius = 8.0f * scale;
    geometry.textLeftInset = scaled(scale, 12.0f);
    geometry.textRightInset = scaled(scale, 10.0f);
    geometry.fontHeight = 13.0f * scale;
    return geometry;
}

bool tabGhostCovers(const TabGhostGeometry& geometry, int px, int py) {
    // Lifted from the per-pixel alpha rebuild in src/tabs.cpp, branch for branch. The left-hand
    // branches measure the distance from `radius`, the right-hand ones from `width - 1 - radius`:
    // those are the same distance mirrored, since (width-1-px) - (width-1-rr) == rr - px, which is
    // what makes the card symmetric and what tab_ghost_tests asserts rather than assumes.
    const float rr = geometry.radius;
    const float w = static_cast<float>(geometry.width);
    const float h = static_cast<float>(geometry.height);
    const float x = static_cast<float>(px);
    const float y = static_cast<float>(py);

    float dx = 0.0f;
    float dy = 0.0f;
    if (x < rr && y < rr) {
        dx = rr - x;
        dy = rr - y;
    } else if (x >= w - rr && y < rr) {
        dx = x - (w - 1.0f - rr);
        dy = rr - y;
    } else if (x < rr && y >= h - rr) {
        dx = rr - x;
        dy = y - (h - 1.0f - rr);
    } else if (x >= w - rr && y >= h - rr) {
        dx = x - (w - 1.0f - rr);
        dy = y - (h - 1.0f - rr);
    }

    // A pixel in none of the four corner squares keeps dx and dy at zero and is therefore INSIDE.
    // That is the whole body of the card, so the early-return-looking shape above is not an
    // optimisation: it is the rule.
    return dx * dx + dy * dy <= rr * rr;
}

TabGhostPlacement tabGhostPlacement(float scale, int cursorScreenX, int cursorScreenY,
                                    int dragOffsetX) {
    // Two clamps in sequence, and they are not redundant. The CEILING keeps the cursor on the card
    // when the tab that was grabbed is wider than the card -- without it a grab near the right edge
    // of a wide tab puts the card entirely left of the pointer. The FLOOR keeps a grab at the very
    // left edge of a tab from parking the cursor on the card's own border, where it reads as a
    // misplaced window rather than as something being carried.
    const int grab = std::min(dragOffsetX, scaled(scale, 166.0f));
    TabGhostPlacement placement;
    placement.x = cursorScreenX - std::max(grab, scaled(scale, 12.0f));
    // Lifted, so the card sits above the pointer rather than under it: the cursor covers the
    // leading edge of the title otherwise, which is the one part of the card carrying information.
    placement.y = cursorScreenY - scaled(scale, 17.0f);
    return placement;
}

bool tabGhostShouldDetach(float scale, float y, float stripHeight) {
    const float band = 48.0f * scale;
    return y > stripHeight + band || y < -band;
}

TabGhostCard tabGhostCard(float scale, const Color& fill, const Color& border, const Color& text) {
    TabGhostCard card;
    card.geometry = tabGhostGeometry(scale);
    card.fill = fill;
    card.border = border;
    card.text = text;
    return card;
}

}  // namespace tinta

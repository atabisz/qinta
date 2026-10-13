#ifndef TINTA_TAB_GHOST_H
#define TINTA_TAB_GHOST_H

#include "geometry.h"

// THE DRAG GHOST'S CONVENTION, SEPARATED FROM THE TWO WAYS OF DRAWING IT.
//
// While a tab is pulled clear of the strip, a translucent card follows the cursor so the detach
// reads as a motion rather than a jump. On Windows that card is a `WS_POPUP` + `WS_EX_LAYERED`
// window painted into a `CreateDIBSection` surface and composited by `UpdateLayeredWindow`; on Qt
// it is a frameless `WA_TranslucentBackground` widget painted by `QPainter`. Those are genuinely
// different mechanisms and neither can be expressed in the other's terms.
//
// What is NOT different is every decision the card embodies: how big it is, how round its corners
// are, which pixels it covers, where it sits relative to the cursor, how opaque it is, and when it
// should appear at all. All of that is arithmetic over one scale factor, and before this header it
// lived as seven literals inside a Win32-only function and four more at its call site -- so the Qt
// arm's only route to the same card was to re-derive eleven numbers and hope.
//
// THIS IS A WINDOWING AND COMPOSITING PORT, NOT A RENDERING ONE, and that is worth saying because
// the project's own headline says "remove Direct2D". The code this header replaces carries no
// `ID2D1`, no `IDWrite` and no `IWIC`. src/input.cpp produced the same finding at 4,394 lines.
//
// WHY THE ARMS ARE NOT DECLARED HERE. Neither `HWND` nor `QWidget` may appear in this file, and
// laundering them through `void*` to satisfy that rule would buy a clean grep at the cost of the
// type safety that makes the rule worth having. So each arm declares itself in its own header --
// include/tab_ghost_qt.h, and include/tab_ghost_win32.h until it was deleted in 0b39ae6 -- each
// honestly named for the platform it needs, and this file holds only what they compute the same way.
namespace tinta {

// The card's size and shape in DEVICE PIXELS at a given scale. `scale` is App::contentScale, which
// is what `dpi(app, v)` multiplies by; passing it explicitly is what makes every number below
// assertable with no App, no window and no platform.
//
// Every field is the shipped Win32 value at scale 1.0. They are not improved here on purpose: this
// is a refactor on that arm, so a changed number would be indistinguishable from a porting mistake.
struct TabGhostGeometry {
    int width;           // 190 at scale 1.0
    int height;          // 34
    float radius;        // 8 -- corner radius, kept float because the coverage test squares it
    int textLeftInset;   // 12
    int textRightInset;  // 10
    float fontHeight;    // 13 -- the MAGNITUDE; the Win32 arm negates it for CreateFontW
};

TabGhostGeometry tabGhostGeometry(float scale);

// Is this pixel inside the rounded card? The rule the Win32 arm's per-pixel alpha rebuild has
// always used, lifted out of its double loop unchanged.
//
// A pixel outside all four corner squares is INSIDE -- the shipped loop leaves dx and dy at zero
// in that case and 0 <= rr*rr holds -- so this is a rounded rectangle and not just four arcs. That
// behaviour is load-bearing and is preserved rather than tidied.
//
// The four branches are not visibly symmetric: the left ones measure from `radius` and the right
// ones from `width - 1 - radius`. They ARE symmetric about (width-1)/2, by substitution, and that
// is asserted rather than believed -- it is the assertion the shipped loop never had.
bool tabGhostCovers(const TabGhostGeometry& geometry, int px, int py);

// How opaque the whole card is, over and above per-pixel alpha. 235/255, the shipped
// BLENDFUNCTION's SourceConstantAlpha. Here rather than in either arm because a card that is 92%
// opaque on one platform and 100% on the other is the exact drift this header exists to prevent.
constexpr unsigned char kTabGhostAlpha = 235;

// Where the card's top-left corner goes, given the cursor in screen coordinates and how far into
// the tab the drag began. Keeps the cursor ON the card even when the tab was wider than the card:
// the grab is clamped to 166 and floored at 12, and the card is lifted 17 above the pointer.
//
// These four numbers were at the call site, not in the ghost's own code, which is why they read as
// incidental. They are the difference between a card that tracks the cursor and one that hangs off
// it, so they belong with the card.
struct TabGhostPlacement {
    int x;
    int y;
};

TabGhostPlacement tabGhostPlacement(float scale, int cursorScreenX, int cursorScreenY,
                                    int dragOffsetX);

// WHEN the card should exist: the tab has been pulled far enough clear of the strip. `y` is the
// pointer in CLIENT coordinates and `stripHeight` is the chrome's top height, both as the Win32 arm
// already had them. The band is 48 either side, and the test is deliberately asymmetric in reading
// only because the strip occupies the top: above the window counts as clear, and so does far enough
// below the strip.
//
// Included here rather than left at the call site for the reason the Vision names: a Qt arm that
// re-derived this threshold would be a second implementation that happens to agree, until it does
// not.
bool tabGhostShouldDetach(float scale, float y, float stripHeight);

// The colours the card draws with, resolved from the theme by the caller. The card does not read a
// theme itself -- it is handed three colours -- because the theme lookup is where App lives and the
// point of this struct is to be constructible without one.
struct TabGhostCard {
    TabGhostGeometry geometry;
    Color fill;    // tabStripBackground(app)
    Color border;  // app.theme.text
    Color text;    // app.theme.text
};

// The title is deliberately NOT a member. The Win32 arm holds it as std::wstring (App::DocTab
// stores it that way) and the Qt arm as QString; putting a third spelling in this struct would add
// a lossy conversion hop to every draw in exchange for nothing, since no portable decision here
// depends on the text.
TabGhostCard tabGhostCard(float scale, const Color& fill, const Color& border, const Color& text);

}  // namespace tinta

#endif  // TINTA_TAB_GHOST_H

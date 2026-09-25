#ifndef TINTA_SCROLL_H
#define TINTA_SCROLL_H

// THE DOCUMENT SCROLL CLAMP, IN A PORTABLE FILE.
//
// This file used to hold the whole document scroll policy -- wheel, arrow keys, page,
// Home and End -- as a COPY of src/input.cpp's numbers, because the Qt window had to
// scroll before input.cpp was routable and a Qt-only copy would have been a divergence
// no test could reach. The plan was that input.cpp would one day route here and the
// copy would become the only one.
//
// IT RESOLVED THE OTHER WAY ROUND, AND THE SEVEN FUNCTIONS ARE GONE AS OF 2026-09-25.
// The key router (slice 3) and the mouse router (slice 4) hand the Qt window's keys and
// wheel to handleKeyDown and handleMouseWheel, so both stacks now run input.cpp's own
// arithmetic and nothing called scrollDocumentByNotches, ...HorizontallyByNotches,
// ...ByStep, ...HorizontallyByStep, ...ByPage, ...ToTop or ...ToBottom. Deleting them
// deletes the second copy; the numbers now exist once, in input.cpp.
//
// WHAT STAYS IS THE CLAMP, because it has a caller input.cpp has no counterpart for:
// QtAppWindow::resizeEvent clamps against the new height before the next paint (see the
// comment there). It is still compiled with NO Qt by portable_compile_only, so a Qt type
// drifting in here fails a build rather than quietly making it unshareable.
//
// IT IS NOT A COPY OF ANY WINDOWS SITE, and it differs from input.cpp in one respect
// worth knowing: it clamps BOTH axes at both ends, whereas input.cpp's arrow keys bound
// targetScrollX at zero only and leave src/paint_document.cpp's prepareDocumentViewport
// to clamp the visible offset. On a resize that difference is invisible -- the paint
// clamps again against the fresh extent -- which is why it is safe to keep here.

struct App;

namespace tinta {

// Clamp both axes into range and mirror the targets onto the live values.
//
// BOTH MAXIMA ARE ZERO UNTIL layoutDocument HAS RUN, because they come from
// app.contentHeight and app.contentWidth, so a clamp before the first layout lands at
// the top-left. That is also what src/file_utils.cpp relies on when it zeroes both
// extents for a freshly loaded document.
void clampDocumentScroll(App& app);

}  // namespace tinta

#endif // TINTA_SCROLL_H

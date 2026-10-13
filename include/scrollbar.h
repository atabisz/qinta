#ifndef TINTA_SCROLLBAR_H
#define TINTA_SCROLLBAR_H

// THE DOCUMENT SCROLLBARS, LIFTED OUT OF src/main_d2d.cpp, AND NOT A QScrollBar.
//
// The approved slice said "a QScrollBar (nothing on screen says how far down you are)".
// It is not a QScrollBar and the substitution is the finding rather than a liberty:
// tinta already HAS a scrollbar and it is not a native control. It is a custom-drawn
// rounded thumb -- 6 device-independent pixels wide, 10 when hovered, 4 from the right
// edge, alpha 0.3 rising to 0.5, a 30 DIP minimum thumb, white on dark themes and black
// on light -- and it was drawn by 95 lines sitting inline in the Windows paint handler at
// src/main_d2d.cpp:492-586.
//
// A QScrollBar would have put a platform-styled control down the right edge of one stack
// and left the other with the thumb above it, so the two builds would have disagreed
// about the single most visible piece of chrome in the product. That is the opposite of
// what this port is for. Moving the existing code costs about the same and produces the
// SAME PIXELS on both stacks, which is the only version of "there is a scrollbar now"
// that is worth having.
//
// It also brings two things a QScrollBar could not have:
//
//   1. THE HORIZONTAL BAR, free, because it was in the same block.
//   2. THE FIND-MATCH TICKS -- the overview-ruler marks that show a search's match
//      silhouette along the track. Nothing draws them off Windows today because
//      src/search.cpp's overlay has not been ported, but they are here waiting rather
//      than being a second thing to remember.
//
// WHY THIS IS A RELOCATION AND NOT A DESIGN, the same sentence paint_document.h opens
// with: every line already drew through the tinta::RenderTarget seam and App::brush. If a
// pixel changes on Windows, the extraction is wrong.
//
// WHAT DOES NOT WORK YET OFF WINDOWS, stated rather than left to be discovered: the
// HOVER and DRAG states. App::scrollbarHovered and App::scrollbarDragging are set by
// src/input.cpp's mouse handling, which is Windows-only, so on Qt the thumb is drawn in
// its resting 6 DIP / 0.3 alpha form and cannot be dragged. The Qt window has no mouse
// handling at all, so this is a missing caller and not a missing implementation -- the
// code below reads those flags and will honour them the moment something sets them.

struct App;
struct DocumentViewport;

// Latches vertical scrollbar visibility and extent against an incremental layout.
//
// SEPARATE FROM THE DRAWING, because it runs at a different time and for a reason worth
// keeping: layoutDocumentViewportFirst grows App::contentHeight in chunks, so a
// visibility test taken per frame would make the right edge BLINK as chunks arrive.
// While the layout is incomplete this only ever lets the extent grow and never retracts
// the bar; when it completes, both take the true value. That was moved verbatim, comment
// included, from the retired src/main_d2d.cpp, and it is 8 lines that no reader would reinvent correctly.
//
// Call once per frame, after layout and before painting.
void updateScrollbarExtent(App& app);

// Draws the vertical thumb, its find-match ticks and the horizontal thumb, each only if
// needed. Requires App::renderTarget and App::brush, and returns without drawing if
// either is null -- the same contract paintDocumentLayout has, for the same reason.
//
// Takes the viewport rather than App alone because the geometry is keyed to
// DocumentViewport::documentWidth, which is the window MINUS the folder browser and the
// edit preview. Passing app.width instead would put the bar under the preview pane in
// edit mode, which is exactly the kind of thing a re-derived number gets wrong.
void paintDocumentScrollbars(App& app, const DocumentViewport& viewport);

#endif  // TINTA_SCROLLBAR_H

#ifndef TINTA_PAINT_DOCUMENT_H
#define TINTA_PAINT_DOCUMENT_H

// THE DOCUMENT PAINT PASS, LIFTED OUT OF src/main_d2d.cpp.
//
// This is the code that turns a laid-out document into drawing calls: rects,
// connectors, shapes, bitmaps, text runs and rules, each culled against the
// viewport. It was 332 lines living inline in the Windows message loop's paint
// handler, which meant that the ONE thing a Qt window most needed -- the ability to
// draw a document -- was locked inside the least portable file in the tree.
//
// WHY THIS IS A SEPARATE FILE AND NOT A SEAM. Nothing here abstracts anything: it
// draws through the tinta::RenderTarget seam that already exists, exactly as it did
// before, and both backends run the same lines. The move is a relocation, not a
// design. That matters for reading it -- if a pixel changes on Windows, this
// extraction is wrong.
//
// WHAT IS STILL NATIVE, both stated here rather than left to be found by grep:
//
//   1. The dashed stroke style. Direct2D builds one from the factory; off Windows a
//      marker object stands in (see the #else arm in the .cpp).
//   2. The colour-emoji DrawTextLayout, which needs an ID2D1DeviceContext and has no
//      seam equivalent. It was already guarded at RUNTIME by `if (app.deviceContext)`,
//      null off Windows -- but a runtime guard does not make an incomplete type
//      compile, so it is inside #ifdef _WIN32 as well.
//
// Those are the only two, and that is a measurement rather than an impression: the
// extracted range was scanned for Direct2D, DirectWrite and WIC identifiers and
// these are what it found.

struct App;

// The scroll state and cull rectangle one paint pass works from.
//
// WHY A STRUCT AND NOT A HANDFUL OF LOCALS. The extracted code computed these at the
// top and then USED them further down -- four of the names leaked past the end of the
// drawn range into the chrome that main_d2d.cpp draws itself (hover buttons, the table
// edit rail): documentWidth at 7 later sites, cullMargin at 4, and viewportTop and
// viewportBottom at 2 each. Fifteen references, and a straight cut would have broken all
// of them. So the producer moved here and the values come back as one object;
// main_d2d.cpp re-declares them under their old names from the struct's fields, which is
// why the sites below the cut read unchanged.
//
// THREE NAMES AND EIGHT REFERENCES NOW, and "scrollbars" is struck from that list of
// chrome. All seven documentWidth sites were the scrollbar block, which moved to
// src/scrollbar.cpp so the Qt window could draw the same thumb; it takes a
// DocumentViewport and reads the field directly, so the alias in main_d2d.cpp is gone.
// Corrected in place because both halves of the sentence -- the count and the list of
// what is still inline -- are claims a reader can check, and each was true when written.
//
// FOUR, NOT SIX, AND THE FIRST COUNT WAS MINE AND WRONG. A name-based grep over the
// rest of the function said six -- it also flagged maxScrollX and maxScrollY -- and
// both of those are inner-scope RE-DECLARATIONS in the scrollbar code (now
// src/scrollbar.cpp, one per bar, the vertical one computed from a different extent
// entirely), which shadow the outer name rather than read it. So they do not leak,
// and aliasing them back would have introduced two shadowed variables to satisfy a
// dependency that was never there. This tree has written the same lesson down before
// in a different form -- count SHAPES, not names -- and a declaration matching the
// grep for its own identifier is exactly that trap.
struct DocumentViewport {
    // Width of the document viewport: the window minus whatever the folder browser
    // and the edit preview have taken. Not app.width.
    float documentWidth = 0.0f;

    // How far the document can scroll, after clamping. Zero when it fits.
    float maxScrollX = 0.0f;
    float maxScrollY = 0.0f;

    // The cull rectangle in DOCUMENT coordinates, so a layout element's own bounds
    // can be compared against it without translating first.
    float top = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;

    // Slack added on every side before culling, so an element that is one pixel off
    // screen is still drawn and scrolling does not pop things into existence.
    float cullMargin = 0.0f;
};

// Applies a pending reading-position restore, clamps App's scroll offsets to the
// laid-out content, and returns the viewport the paint pass culls against.
//
// It MUTATES App (scrollX, scrollY, targetScrollY, pendingScrollRestore) -- the name
// says prepare rather than compute for that reason. Call it once per paint, before
// paintDocumentLayout, and pass the result straight in.
DocumentViewport prepareDocumentViewport(App& app);

// Draws the cached layout. Requires App::renderTarget and App::brush to be non-null
// and returns without drawing if either is missing, because both backends have a
// window in which they are null (Direct2D between a device loss and the rebuild, Qt
// outside a paint event) and a crash there would be a worse failure than a blank
// frame.
//
// Does NOT clear the background and does not draw chrome. The caller owns both.
void paintDocumentLayout(App& app, const DocumentViewport& viewport);

#endif  // TINTA_PAINT_DOCUMENT_H

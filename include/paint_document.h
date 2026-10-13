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
// WHAT IS STILL NATIVE. ONE THING, stated here rather than left to be found by grep:
//
//   1. The dashed stroke style. Direct2D builds one from the factory; off Windows a
//      marker object stands in (see the #else arm in the .cpp).
//
// That is the only one, and it is a measurement rather than an impression: the extracted
// range was scanned for Direct2D, DirectWrite and WIC identifiers and this is what it
// found. Re-measured 2026-09-19 --
// `rg 'nativeTextLayout|text_d2d\.h|app\.native|deviceContext|IDWrite' src/paint_document.cpp`
// returns 5 matches and NOT ONE IS A CODE LINE. Three comment on the removal (lines 16, 23
// and the block above the seam call); the other two, at 88 and 94, are about
// app.nativeDashedStrokeStyle -- entry 1 above, a different subject, and the one native thing
// that is supposed to still be discussed here. This said "only comments about the removal"
// until 2026-09-19, which described 3 of the 5 and quietly folded entry 1's evidence into the
// deletion's. Read the matches; a total is not a classification.
//
// THIS LIST HAD A SECOND ENTRY AND THE ENTRY WAS FALSE. It read: "The colour-emoji
// DrawTextLayout, which needs an ID2D1DeviceContext and has no seam equivalent." The seam
// has carried tinta::DrawTextOptions::EnableColorFont since it was built -- see
// include/text_api.h -- so the native branch was a duplicate of an existing seam path, and
// it was deleted on 2026-09-19. Four comments in this tree said that same thing about the
// same code, each citing the others, and none of them was ever re-probed. Worth knowing
// when reading the remaining entry above: a list of what is "still native" is a claim with
// an expiry date, and this file's copy outlived its truth by months.
//
// A CONSEQUENCE WORTH KNOWING BEFORE IT IS FILED AS A BUG. This pass now asks for colour
// fonts and almost nothing else does. Counted 2026-09-19: `rg -c 'DrawTextLayout\(' src`
// returns 33 textual hits, of which 3 are backend definitions or forwards (src/render_d2d.cpp
// twice, src/render_qt.cpp once) and 1 is a comment -- leaving 29 call sites, of which TWO
// pass tinta::DrawTextOptions::EnableColorFont (here and src/math_render.cpp) and 27 pass
// nothing at all. On Direct2D, where the flag is what gates colour, that means the same emoji
// renders in COLOUR in the document pane and MONOCHROME in the editor pane (src/editor.cpp,
// five option-less sites), the overlays (seven) and the tab strip. Qt shows no such split,
// because that backend paints colour glyphs whether or not the flag arrives.
//
// This is a real inconsistency and it is deliberately not fixed here -- widening it is a
// behaviour change for each pane that wants its own decision, not a side effect of deleting a
// duplicate branch. Two places to start if it is taken up: src/editor.cpp's five sites, and
// the sole DrawTextLayout in src/export_docx.cpp -- a second exporter that omits the flag
// exactly as src/print.cpp does but carries no comment saying the omission is on purpose.
//
// The line number that used to sit in that sentence is dropped rather than refreshed. It
// said :556, the call is at :561 today, and it was at :539 when the sentence was written
// -- so it was never right, and it has since been moved again by an edit that changed no
// code in that file at all (2026-09-20, three dead includes deleted). One symbol grep
// finds the call and cannot go stale; "the sole DrawTextLayout" is also a checkable claim
// where a number is not, because if a second one appears the phrase stops resolving.

struct App;

// The scroll state and cull rectangle one paint pass works from.
//
// WHY A STRUCT AND NOT A HANDFUL OF LOCALS. The extracted code computed these at the
// top and then USED them further down -- four of the names leaked past the end of the
// drawn range into the chrome the frame draws itself (hover buttons, the table edit
// rail), then in the retired main_d2d.cpp: documentWidth at 7 later sites, cullMargin at
// 4, and viewportTop and viewportBottom at 2 each. Fifteen references, and a straight cut
// would have broken all of them. So the producer moved here and the values come back as
// one object; renderFrame (src/render_frame.cpp) re-declares them under their old names
// from the struct's fields, which is why the sites below the cut read unchanged.
//
// THREE NAMES AND EIGHT REFERENCES NOW, and "scrollbars" is struck from that list of
// chrome. All seven documentWidth sites were the scrollbar block, which moved to
// src/scrollbar.cpp so the Qt window could draw the same thumb; it takes a
// DocumentViewport and reads the field directly, so the alias in renderFrame is gone.
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

// Creates App::dashedStrokeStyle if it does not exist yet, and returns nothing -- the
// caller reads the member afterwards. Idempotent: a second call is a null check and a
// return, which is the "create once, reuse every frame" latch the paint pass relied on
// when this lived inline there.
//
// TWO CALLERS, AND THEY DISAGREE ABOUT WHEN, WHICH IS WHY THE TEST IS NOT IN HERE.
// paintDocumentLayout calls it only when the laid-out document actually contains a
// dashed connector, so a document with none never allocates a factory object. The DOCX
// exporter has no connector list to interrogate -- it walks mermaid prims -- so it calls
// this unconditionally. That policy therefore stays at each call site; folding the
// std::any_of in here would have forced a second entry point for the exporter.
//
// It is declared here rather than left file-local because the exporter needs it, and the
// exporter must not grow an #ifdef of its own: choosing between the Direct2D factory and
// the portable marker is this pass's job, not the exporter's.
void ensureDashedStrokeStyle(App& app);

#endif  // TINTA_PAINT_DOCUMENT_H

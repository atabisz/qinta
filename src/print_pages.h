#ifndef TINTA_PRINT_PAGES_H
#define TINTA_PRINT_PAGES_H

// What src/print.cpp shares with its two backend files, and nothing else. Internal, in src/
// beside xps_page_emitter.h for the same reason: no caller outside the print code should see
// a page loop.
//
// print.cpp is portable, and the two hooks below plus printDocument (include/print.h) are
// where it reaches a backend. The Windows half
// (src/print_win32.cpp) is the printer dialog and the XPS print job. The Qt half
// (src/print_qt.cpp) refuses printing, because a QPrinter arm needs a CJK print fixture
// before anything can tell a correct page from a plausible one.

#include "print.h"
#include "pdf_export.h"  // tinta::PagePainter

#include <functional>

struct PrintPageGeometry {
    float pageW, pageH;        // full page in DIPs
    float contentW, contentH;  // inside the margins
};

inline constexpr float kPrintPageMarginDips = 72.0f;  // 0.75 in

// Lays the document out at the page's content width in the print palette, paginates it, and
// hands each page to emitPage as a painter. emitPage picks the surface, calls the painter
// once, and returns whether that page was emitted. Returns the page count, or -1 on any
// failure. The screen view is restored before it returns.
int renderPrintPages(App& app, const PrintPageGeometry& geo,
                     const std::function<bool(int, const tinta::PagePainter&)>& emitPage);

// BACKEND HOOK: true when the user's locale measures in US customary units, which makes
// Letter the default paper. A4 otherwise, including for UK imperial: Windows'
// LOCALE_IMEASURE has no UK value either, so both arms answer the same question.
bool printLocalePrefersLetter();

// BACKEND HOOK: a render target the print code may seat as app.renderTarget when App has
// none, or nullptr. Every print entry point runs from an input handler or a timer, and the
// two stacks differ there. On Windows app.renderTarget is the window's target for the whole
// session. On Qt it exists only inside a paint event (QtScopedRenderTarget), so Ctrl+P, a
// PDF export and the link peek's dwell timer would all find it null. The print code needs a
// target only as a FACTORY -- for tinta::rasterizeToImage, CreateBitmap and
// beginPdfDocument, and for the bitmaps layout creates -- and never draws on it. So the Qt
// arm hands back one over an offscreen image.
tinta::RenderTarget* printOffscreenTarget();

#endif  // TINTA_PRINT_PAGES_H

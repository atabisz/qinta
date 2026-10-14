#ifndef TINTA_PRINT_H
#define TINTA_PRINT_H

#include "app.h"

#include <vector>

// Prints the current document. The user picks any installed printer from the
// standard dialog. Rendering uses a dedicated light print palette regardless of
// the active theme.
//
// A BACKEND HOOK since 2026-10-06, with one arm now: src/print_qt.cpp prints through
// QPrinter and QPrintDialog on every platform (it refused with a chip until the QPrinter
// slice, 2026-10-06; the retired src/print_win32.cpp was the ID2D1PrintControl arm). Everything
// else in this header is defined once, in the portable src/print.cpp.
bool printDocument(App& app);

// Print preview overlay: Ctrl+P opens it, the user flips through the
// paginated pages, then confirms into the printer dialog or cancels.
// Pages are previewed at the default printer's paper size (A4 fallback);
// printDocument re-paginates for whatever printer is finally chosen.
void openPrintPreview(App& app);
void closePrintPreview(App& app);
void printPreviewSetPage(App& app, int page);   // clamps and re-rasterizes
void printPreviewConfirm(App& app);  // close overlay, run the dialog

// Paper formats offered by the preview. The choice is preset into the
// printer dialog's DEVMODE, and the dialog's final answer still wins.
struct PrintPaper {
    const wchar_t* name;
    float w, h;      // portrait DIPs at 96/inch
    short dmPaper;   // DMPAPER_* constant
};
inline constexpr PrintPaper PRINT_PAPERS[] = {
    {L"A4",     794.0f,  1123.0f, 9},   // DMPAPER_A4
    {L"Letter", 816.0f,  1056.0f, 1},   // DMPAPER_LETTER
    {L"Legal",  816.0f,  1344.0f, 5},   // DMPAPER_LEGAL
    {L"A3",     1123.0f, 1587.0f, 8},   // DMPAPER_A3
};
inline constexpr int PRINT_PAPER_COUNT = 4;

// Re-paginates the open preview for a new paper size / orientation
void printPreviewSetFormat(App& app, int paper, bool landscape);

// Page break positions in document Y for pages `pageContentH` tall, over the current layout.
// Breaks are pulled above any line, image, shape, code block or table row they would cut,
// unless that atom is taller than most of a page. Declared for tests/page_break_tests.cpp.
std::vector<float> computePageBreaks(const App& app, float pageContentH);

// Debug/testing: renders each paginated page to <outDir>/page-N.png using
// the same layout and pagination as printing. Returns the page count, or -1.
//
// PORTABLE BODY, like renderPeekBitmap below and for the same reason: it draws
// through tinta::rasterizeToImage, encodes through tinta::encodePng and writes
// with std::ofstream, so nothing in it names Direct2D or WIC. What unblocked it
// was inverting src/print.cpp's page loop -- it used to be handed a closed
// ID2D1CommandList to replay, which no seam offers and none should.
//
// PAGE HASHES SURVIVED THE PORT, MEASURED, and this paragraph said the opposite
// before the measurement was taken. Several slices in this tree use these PNGs as
// a byte-identity falsifier, so it matters: 140 README pages and a 3-page fixture
// are byte-identical across the change (2026-09-20), which means a hash captured
// before it IS comparable with one captured after. The draft comment predicted a
// break, on the reasoning that the old body rasterised on the GPU and
// tinta::rasterizeToImage is always software. The first half was wrong.
// app.useHardwareRT (include/app.h) starts FALSE and only flips on
// WM_APP_GPU_READY, and the retired src/main_d2d.cpp ran this function and returned ~40 lines
// before it even started the GPU warm-up -- so on the harness path the old body's
// ID2D1Device sat behind a SOFTWARE hwnd target as well. Having a device context
// in hand says nothing about which rasteriser is under it.
//
// WHAT DID CHANGE is who decides: the old body inherited app.useHardwareRT, so a
// caller reaching this from a warm GUI would have rasterised on the GPU, while
// CreateWicBitmapRenderTarget is software unconditionally. The harness never was
// that caller, so no hash moved -- but the output is now a function of the build
// rather than the driver for ANY caller, which is what an instrument wants.
// Separately: the path separator is written by std::filesystem rather than as a
// literal backslash.
int printDebugPages(App& app, const std::wstring& outDir);

// Link peek: renders the top of another markdown file at the live theme
// into a 2x bitmap. heightDips shrinks to the target's content height when
// the document is shorter than the request.
//
// Returns a SEAM HANDLE, and the ownership note that used to read "(caller
// releases)" is gone with the raw pointer -- the handle releases itself. The
// BODY has now crossed too: it draws through tinta::rasterizeToImage and takes
// the pixels back through RenderTarget::CreateBitmap at 192 dpi, so nothing in
// it names Direct2D. This comment used to say that routing it "WOULD make this
// function portable outright ... at the cost of a software rasteriser plus two
// full-buffer copies per dwell"; the cost was measured at the real panel size
// (2.68 ms Direct2D, 4.19 ms Qt, both copies included, against a 25 ms budget
// fixed beforehand) and it is recorded beside the CreateBitmap exclusion in
// render_api.h, which is still where that trade is weighed rather than here.
//
// PORTABLE BODY, NOT A PORTABLE FILE: the rest of src/print.cpp keeps
// ID2D1PrintControl, DEVMODE and the printer dialog, so this function no longer
// holds the file back but nothing else moved with it. (A PORTABLE FILE SINCE 2026-10-06:
// those three moved to src/print_win32.cpp, and qinta's link peek runs this body.)
std::shared_ptr<tinta::Bitmap> renderPeekBitmap(App& app, const std::wstring& path,
                                                float widthDips, float& heightDips);

#endif // TINTA_PRINT_H

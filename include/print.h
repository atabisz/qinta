#ifndef TINTA_PRINT_H
#define TINTA_PRINT_H

#include "app.h"

// Prints the current document through the Windows print pipeline
// (ID2D1PrintControl). The user picks any installed printer — including
// "Microsoft Print to PDF" — from the standard dialog. Rendering uses a
// dedicated light print palette regardless of the active theme.
bool printDocument(App& app);

// Print preview overlay: Ctrl+P opens it, the user flips through the
// paginated pages, then confirms into the printer dialog or cancels.
// Pages are previewed at the default printer's paper size (A4 fallback);
// printDocument re-paginates for whatever printer is finally chosen.
void openPrintPreview(App& app, HWND hwnd);
void closePrintPreview(App& app, HWND hwnd);
void printPreviewSetPage(App& app, int page);   // clamps and re-rasterizes
void printPreviewConfirm(App& app, HWND hwnd);  // close overlay, run the dialog

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

// Debug/testing: renders each paginated page to <outDir>\page-N.png using
// the same layout and pagination as printing. Returns the page count.
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
// holds the file back but nothing else moved with it.
std::shared_ptr<tinta::Bitmap> renderPeekBitmap(App& app, const std::wstring& path,
                                                float widthDips, float& heightDips);

#endif // TINTA_PRINT_H

#ifndef TINTA_PDF_EXPORT_H
#define TINTA_PDF_EXPORT_H

#include <functional>
#include <memory>
#include <string>

#include "render_api.h"

// The platform layer's seventh seam: writing a paginated document to a PDF file.
//
// Nothing here may include <windows.h>, "app.h", or any Direct2D, WIC or XPS header,
// and the same sentence in platform_paths.h says why at length. The narrower reason
// this seam exists at all is measured rather than assumed: src/print.cpp is absent
// from QINTA_APP_SOURCES, so the Qt app has no PDF export whatsoever, and tinta's
// link line carries no Qt while qinta's carries no d2d1. Nothing could be shared by
// including a file across that boundary even if the code were willing.
//
// WHAT IS *NOT* BEHIND THIS SEAM, stated because the file it came out of is bigger
// than what moved. Pagination, print layout and the page painter all stay in
// src/print.cpp: renderPages touches App's layout state and the page geometry and
// names no backend type, which is exactly what made it reusable and what makes this
// header small. What moved is the SINK -- where a painted page goes.
//
// Paths and titles are std::wstring to match the rest of the tree, which
// platform_paths.h already argues for and which this header does not re-litigate.
namespace tinta {

// Paints one page into whatever target the sink supplies. `brush` may be null, and a
// painter that is handed null must draw nothing.
//
// THE TYPE IS image_api.h's rasterizeToImage `draw` PARAMETER, SPELLED OUT RATHER THAN
// APPROXIMATED, and that identity is load-bearing in two places rather than a tidy
// coincidence: print.cpp's printDebugPages hands a painter straight to the raster seam
// with no adapter, and the same painter reaches this seam unchanged. If this alias and
// image_api.h ever disagree, one of the two seam calls stops compiling, which is the
// cheapest guard available on the agreement.
using PagePainter = std::function<void(RenderTarget&, Brush*)>;

// A document being written, one page at a time. Obtained from beginPdfDocument below
// and closed by destruction.
//
// A SINK RATHER THAN A ONE-SHOT CALL, because the page loop is on the other side. The
// alternative -- handing the backend a page count and a painter factory -- would have
// moved pagination behind the seam, and pagination is the half of src/print.cpp that
// is already portable. So the caller keeps the loop and the backend keeps the surface,
// which is the same division drawDocumentRange already lives under.
class PdfPageSink {
public:
    virtual ~PdfPageSink() = default;

    // Appends one page of `pageW` x `pageH` DIPs and calls `paint` exactly once to
    // fill it. The page is cleared to white before `paint` runs -- a printed page is
    // white on every theme, and clearing HERE rather than in the painter is the same
    // choice rasterizeToImage makes and for the same reason: neither backend may hand
    // a painter an uninitialised surface. Returns false on any failure, and a false
    // return means the document is no longer worth finishing.
    virtual bool addPage(float pageW, float pageH, const PagePainter& paint) = 0;

    // Completes the document. Must be called before destruction for the file to be
    // valid; destruction alone releases resources and flushes whatever was written,
    // which for an unfinished document is not a readable PDF. Returns false if the
    // document could not be completed.
    virtual bool finish() = 0;

protected:
    PdfPageSink() = default;
    PdfPageSink(const PdfPageSink&) = delete;
    PdfPageSink& operator=(const PdfPageSink&) = delete;
};

// Opens `path` for writing and returns a sink for its pages, or nullptr if the file
// or the backend document could not be created.
//
// `target` is the live render target, and it is here because the Windows arm needs a
// device it can create a print control on. That is the established idiom for a
// portable seam recovering its own backend -- src/image_win32.cpp does
// dynamic_cast<D2DRenderTarget*> then native(), and asserts rather than tolerates a
// foreign target. The two rejected alternatives are worth naming because each looks
// reasonable for one line: passing App& would drag the app's whole header, and
// therefore windows.h, into a portable seam; passing a factory would put
// IWICImagingFactory in this file.
//
// `documentTitle` is the document's name. The Windows arm spends it as the XPS print
// job name; a QPdfWriter arm would set it as the PDF title.
//
// NULLPTR STILL LEAVES A FILE AT `path`. The Windows arm creates the stream before it
// can know whether a print control is obtainable, and this is the behaviour the code
// had inline before the seam existed -- preserved deliberately rather than tidied,
// because the acceptance test for the extraction was byte-identity and a caller that
// wants the failed file gone can delete it.
std::unique_ptr<PdfPageSink> beginPdfDocument(RenderTarget& target,
                                              const std::wstring& path,
                                              const std::wstring& documentTitle);

}  // namespace tinta

#endif  // TINTA_PDF_EXPORT_H

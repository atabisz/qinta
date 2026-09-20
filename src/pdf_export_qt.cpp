#include "pdf_export.h"

#include "render_qt.h"

#include <QFile>
#include <QMarginsF>
#include <QPageSize>
#include <QPdfWriter>

// The Qt arm of the PDF seam, and the second implementation that turns
// include/pdf_export.h from an interface into a contract. QPdfWriter writes the PDF
// itself rather than routing a print job at a driver, which is the one structural
// difference from the Windows arm and the source of every other difference below.
//
// WHAT THIS ARM DOES NOT DO, AND THE WINDOWS ARM GETS FOR FREE: font embedding is
// QPdfWriter's own business here, where the Windows arm delegates it to the
// "Microsoft Print to PDF" driver. src/pdf_export_win32.cpp says that is why
// printDocument is a later slice; the same sentence applies to this file from the
// other side. Nothing here has been measured against a CJK document yet, and that
// measurement is the gate on the printDocument port rather than on this arm.
//
// `Qt6::Gui` IS THE WHOLE LINK COST -- QPdfWriter lives in QtGui, not in
// Qt6::PrintSupport, measured at C:/Qt/6.8.3/msvc2022_64/include/QtGui/qpdfwriter.h.
// So this arm adds no Qt component to any target that already draws.

namespace tinta {
namespace {

// 1 DIP is 1/96 inch, which is what the whole tree means by a coordinate: the page
// sizes reaching addPage come from print.cpp's PageGeometry in the same units the
// Direct2D arm hands to ID2D1PrintControl::AddPage.
//
// SETTING THE WRITER'S RESOLUTION TO 96 IS WHAT MAKES THE PAINTER'S UNIT A DIP, and
// it is the reason no transform appears anywhere in this file. QPdfWriter's painter
// works in device pixels at the writer's resolution, so at 96 the painter's unit and
// the seam's unit are the same thing and every drawing call arrives unscaled. The
// alternative -- leaving the default resolution and scaling in addPage -- would put a
// transform under every page painter, where a caller that sets its own transform
// (drawDocumentRange does) would silently compose with it.
constexpr int kDipsPerInch = 96;

class QtPdfPageSink final : public PdfPageSink {
public:
    // Member order is load-bearing: `file_` must outlive `writer_`, because the writer
    // borrows the device and flushes into it while being destroyed. Declared in that
    // order and destroyed in the reverse, which is the only ordering that works.
    explicit QtPdfPageSink(const QString& path) : file_(path) {}

    ~QtPdfPageSink() override {
        // The painter ends here for a document nobody finished. QPdfWriter needs its
        // painter destroyed before it will write the trailer, so an abandoned document
        // leaves a truncated file rather than a valid empty one -- which is the same
        // promise include/pdf_export.h already makes about the Windows arm ("destruction
        // alone releases resources and flushes whatever was written, which for an
        // unfinished document is not a readable PDF").
        target_.reset();
    }

    bool open(const std::wstring& documentTitle) {
        // OUR OWN QFile RATHER THAN QPdfWriter's FILENAME CONSTRUCTOR, and this is the
        // one place the Qt arm is deliberately less convenient than it could be.
        // QPdfWriter(QString) opens the file itself and reports nothing: an unwritable
        // path yields a live writer whose first paint goes nowhere, so the seam would
        // return a sink and fail later. Opening the QFile here makes the failure
        // arrive where the seam's contract says it does -- as nullptr out of
        // beginPdfDocument -- and it is checked by the shared assertions, which run
        // this same case against the Windows arm.
        //
        // A CONSEQUENCE WORTH NAMING: on failure this leaves NO file behind, where the
        // Windows arm leaves an empty one (include/pdf_export.h documents that, having
        // preserved it deliberately). The seam's promise is only about the return
        // value, so both are conformant, and the shared assertions therefore assert on
        // the nullptr and not on the filesystem.
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

        writer_ = std::make_unique<QPdfWriter>(&file_);
        writer_->setTitle(QString::fromStdWString(documentTitle));
        writer_->setResolution(kDipsPerInch);
        return true;
    }

    bool addPage(float pageW, float pageH, const PagePainter& paint) override {
        if (!writer_) return false;
        if (pageW <= 0.0f || pageH <= 0.0f) return false;

        // THE PAGE SIZE HAS TO BE SET BEFORE THE PAINTER EXISTS, which is why the
        // target is built here on the first page instead of in open(). QPdfWriter fixes
        // the paintable area when QPainter::begin runs, and beginPdfDocument is never
        // told a page size -- only addPage is. So the sink cannot construct its
        // QtRenderTarget until it has seen a page, and it must then keep that ONE
        // target for every remaining page: QtRenderTarget begins its painter in the
        // constructor and ends it in the destructor (include/render_qt.h:51-56), and
        // ending the painter is what writes the PDF trailer. A target per page would
        // produce a one-page document and three-quarters of a file.
        //
        // This is the exact inverse of the Windows arm, where XpsPageEmitter builds a
        // fresh ID2D1CommandList per page and holds nothing across them. The seam's
        // addPage contract is satisfiable both ways, which is the thing this arm was
        // written to find out.
        if (!setPageGeometry(pageW, pageH)) return false;
        if (!target_) {
            target_ = std::make_unique<QtRenderTarget>(*writer_);
        } else if (!writer_->newPage()) {
            return false;
        }

        // White, before the painter sees the page, because the seam says so and for the
        // reason it gives: neither backend may hand a painter an uninitialised surface.
        // The Windows arm's equivalent is XpsPageEmitter's dc->Clear.
        //
        // QtRenderTarget::Clear uses CompositionMode_Source to match D2D's
        // replace-don't-blend semantics. The PDF paint engine may decline that mode and
        // fall back to SourceOver; for an opaque white over a blank page the result is
        // identical either way, so this is noted rather than worked around -- but it is
        // the one call in this file that can put a Qt warning on stderr.
        target_->Clear(Color{1.0f, 1.0f, 1.0f, 1.0f});

        // Black, matching the brush XpsPageEmitter creates for the same purpose. The
        // colour is not decorative here the way the Direct2D construction colour turned
        // out to be: drawDocumentRange calls SetColor before every use, so this value is
        // only ever seen by a painter that does NOT set its own colour.
        QtBrush brush(Color{0.0f, 0.0f, 0.0f, 1.0f});
        paint(*target_, &brush);
        ++pagesEmitted_;
        return true;
    }

    bool finish() override {
        // Destroying the target ends the QPainter, and ending the QPainter is what makes
        // QPdfWriter write the trailer. Until that happens the file on disk is not a
        // readable PDF, so this is the whole of finish() and not a formality.
        if (!target_) return false;  // nothing was ever painted; there is no document
        target_.reset();
        const bool ok = file_.flush();
        file_.close();
        return ok;
    }

private:
    // Per-page rather than once, because the seam allows a document whose pages differ
    // in size and print.cpp is free to produce one. QPageSize in inches is the exact
    // conversion from DIPs at this writer's resolution; margins are zeroed because Qt
    // applies a default page margin that would both shift the painter's origin and
    // shrink its viewport, silently insetting every page by a few millimetres.
    bool setPageGeometry(float pageW, float pageH) {
        const QPageSize size(QSizeF(pageW / kDipsPerInch, pageH / kDipsPerInch),
                             QPageSize::Inch);
        if (!writer_->setPageSize(size)) return false;
        return writer_->setPageMargins(QMarginsF(0, 0, 0, 0));
    }

    QFile file_;
    std::unique_ptr<QPdfWriter> writer_;
    std::unique_ptr<QtRenderTarget> target_;
    int pagesEmitted_ = 0;
};

}  // namespace

std::unique_ptr<PdfPageSink> beginPdfDocument(RenderTarget& target,
                                              const std::wstring& path,
                                              const std::wstring& documentTitle) {
    // `target` IS DELIBERATELY UNUSED, AND NOT ASSERTED ON. The parameter exists
    // because the Windows arm needs a device to create a print control on, which
    // include/pdf_export.h states at the declaration. QPdfWriter needs no device at
    // all, so this arm has nothing to recover from the argument.
    //
    // The Windows arm asserts its target's dynamic type; mirroring that here would be
    // a constraint invented to look symmetrical. It would reject a target this backend
    // never touches, and it would make a shared assertion about foreign targets pass
    // for two unrelated reasons. So: unused, said out loud.
    (void)target;

    auto sink = std::make_unique<QtPdfPageSink>(QString::fromStdWString(path));
    if (!sink->open(documentTitle)) return nullptr;
    return sink;
}

}  // namespace tinta

// The Qt harness for the shared PDF-seam assertions.
//
// The assertions are in tests/pdf_export_shared.cpp and are run unchanged by
// tests/pdf_export_win32_tests.cpp against the XPS arm. That split is the point, and it
// is the same split tests/render_api_tests.cpp and tests/render_d2d_tests.cpp already
// use for the drawing seam: two backends with their own test files would each prove only
// that they are self-consistent, and the failure worth catching is the two disagreeing
// about include/pdf_export.h.
//
// Nothing below asserts anything. It supplies a render target, a scratch directory and a
// name.

#include "pdf_export_shared.h"
#include "render_qt.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>

#include <memory>

namespace {

class QtPdfHarness final : public PdfHarness {
public:
    // A LIVE TARGET THIS BACKEND WILL NOT LOOK AT, and supplying one anyway is the
    // harness earning its keep. src/pdf_export_qt.cpp ignores the argument -- QPdfWriter
    // needs no device -- while the other arm recovers a device context from it. The
    // shared assertions must not know which of those they are running against, so the
    // parameter is filled with something real on both sides.
    //
    // 8x8 because the size is irrelevant to a target nobody draws into, and a comment
    // saying so is cheaper than a future reader wondering what the dimensions mean.
    QtPdfHarness() : image_(8, 8, QImage::Format_ARGB32) {
        image_.fill(Qt::transparent);
        target_ = std::make_unique<QtRenderTarget>(image_);
    }

    tinta::RenderTarget& target() override { return *target_; }

    std::wstring scratchDir() override { return QDir::tempPath().toStdWString(); }

    const char* name() const override { return "qt"; }

private:
    QImage image_;
    std::unique_ptr<QtRenderTarget> target_;
};

}  // namespace

int main(int argc, char** argv) {
    // QGuiApplication rather than QCoreApplication: QPdfWriter is a QPaintDevice and the
    // paint engine reaches the font database when it opens a document, whether or not any
    // text is drawn.
    //
    // NO FONT-DATABASE REFUSAL HERE, unlike tests/render_api_tests.cpp, and the
    // difference is worth one sentence: that suite MEASURES text, so an empty font
    // database would let it report fabricated metrics as a pass. Nothing in the PDF
    // assertions lays out a glyph -- they read page counts, magic bytes, file sizes and a
    // MediaBox -- so an empty database cannot make any of them look green while measuring
    // nothing. CJK font embedding, which is the reading that WOULD need such a guard, is
    // the printDocument slice's problem and is deliberately not claimed here.
    QGuiApplication app(argc, argv);

    QtPdfHarness harness;
    return runPdfExportAssertions(harness) == 0 ? 0 : 1;
}

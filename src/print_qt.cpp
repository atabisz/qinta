#include "print_qt.h"

#include "print_pages.h"
#include "render_qt.h"

#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintDialog>
#include <QPrinter>

#include <memory>

// THE QT ARM OF src/print.cpp's TWO BACKEND HOOKS: the paper locale, and printDocument.
//
// PRINTING WORKS HERE SINCE 2026-10-06. Until then printDocument refused with a chip, and the
// reason given was that a QPrinter arm "waits on a CJK print fixture that can tell a correct
// page from a plausible one". tests/fixtures/print-cjk.md is that fixture, and
// tests/qt_entry_tests.cpp prints it through printDocumentTo below into a PDF and checks that a
// CJK-capable font program was embedded. The dialog itself is the one part no test reaches.

bool printLocalePrefersLetter() {
    // ImperialUSSystem only. ImperialUKSystem means A4 paper, as LOCALE_IMEASURE's '0' does
    // for a UK locale on the Windows arm.
    return QLocale::system().measurementSystem() == QLocale::ImperialUSSystem;
}

namespace {

// 1 DIP is 1/96 inch, the unit every coordinate in the tree is in.
constexpr double kDipsPerInch = 96.0;

}  // namespace

bool printDocumentTo(App& app, QPrinter& printer) {
    if (!app.root) return false;

    // THE PAPER CORNER IS THE ORIGIN, as on the Windows arm, whose XPS print control addresses
    // the physical page. tinta lays its own 0.75 in margins inside the page (print_pages.h), so
    // letting Qt inset the painter by the printer's margins as well would print a double margin.
    printer.setFullPage(true);
    const QRectF page = printer.pageLayout().fullRect(QPageLayout::Inch);
    PrintPageGeometry geo{};
    geo.pageW = static_cast<float>(page.width() * kDipsPerInch);
    geo.pageH = static_cast<float>(page.height() * kDipsPerInch);
    if (geo.pageW < 200.0f || geo.pageH < 200.0f) {  // a driver reporting nonsense, as on Windows
        geo.pageW = 794.0f;
        geo.pageH = 1123.0f;
    }
    geo.contentW = geo.pageW - kPrintPageMarginDips * 2;
    geo.contentH = geo.pageH - kPrintPageMarginDips * 2;

    // ONE TARGET FOR THE WHOLE JOB, and newPage between pages, for the reason
    // src/pdf_export_qt.cpp gives: QtRenderTarget begins its QPainter in the constructor and
    // ends it in the destructor, and ending the painter is what ends the print job.
    //
    // THE PAINTER IS IN DEVICE PIXELS AT THE PRINTER'S RESOLUTION, not in DIPs, and unlike the
    // PDF arm this one cannot set a resolution of 96: a real printer reports its own, and asking
    // for another would be asking the driver to resample. So every page draws under a
    // DIP-to-device scale, and drawDocumentRange composes its shrink bands with that transform
    // rather than replacing it.
    const float scale = static_cast<float>(printer.resolution() / kDipsPerInch);
    std::unique_ptr<QtRenderTarget> target;
    const int pages = renderPrintPages(
        app, geo, [&](int, const tinta::PagePainter& paint) {
            if (!target) {
                target = std::make_unique<QtRenderTarget>(printer);
            } else if (!printer.newPage()) {
                return false;
            }
            target->SetTransform(tinta::matrixScale(scale, scale));
            // Black, matching the brush the PDF and XPS arms hand their painters.
            QtBrush brush(tinta::Color{0.0f, 0.0f, 0.0f, 1.0f});
            paint(*target, &brush);
            return true;
        });
    const bool painted = target != nullptr;
    target.reset();  // ends the painter, which ends the job
    return pages > 0 && painted;
}

bool printDocument(App& app) {
    if (!app.root) return false;

    QPrinter printer(QPrinter::HighResolution);
    // The preview's paper and orientation as the dialog's starting point, as the Windows arm
    // presets DEVMODE; whatever the user picks in the dialog wins, because printDocumentTo
    // reads the page back from the printer rather than from the preview.
    if (app.printPreviewPaper >= 0 && app.printPreviewPaper < PRINT_PAPER_COUNT) {
        static const QPageSize::PageSizeId kIds[PRINT_PAPER_COUNT] = {
            QPageSize::A4, QPageSize::Letter, QPageSize::Legal, QPageSize::A3};
        printer.setPageSize(QPageSize(kIds[app.printPreviewPaper]));
        printer.setPageOrientation(app.printPreviewLandscape ? QPageLayout::Landscape
                                                             : QPageLayout::Portrait);
    }
    printer.setDocName(QStringLiteral("Qinta - ") + QString::fromStdString(app.currentFile));

    QPrintDialog dialog(&printer);
    if (dialog.exec() != QDialog::Accepted) return false;  // cancelled
    return printDocumentTo(app, printer);
}

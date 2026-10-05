#include "print_pages.h"
#include "render_qt.h"
#include "signals.h"

#include <QImage>
#include <QLocale>

#include <cstdio>

// THE QT ARM OF src/print.cpp's THREE BACKEND HOOKS. Until 2026-10-06 this file defined seven
// print symbols, and every one refused: preview, PDF export and the link peek included. Those
// now live in print.cpp itself, which compiles off Windows. What is left is the three questions
// only a platform can answer: printDocument (include/print.h) and the two in src/print_pages.h.
//
// PRINTING STILL REFUSES, AND THAT IS THE ONE STATED GAP. A QPrinter arm is a Qt6::PrintSupport
// dependency plus a dialog nobody can drive from a test. Its correctness question is font
// embedding, which needs a CJK print fixture that can tell a correct page from a plausible
// one. The preview and the PDF are real on this stack, so a user reaches the refusal only by
// pressing Print in the preview, and the preview has already shown them the pages.
//
// A REFUSAL IS A CHIP AND A LINE ON stderr, the same two channels src/main_qt.cpp's refused
// flags use. The chip text is ENGLISH ONLY. It is not an i18n key, because translating a
// message that exists to be deleted would add four strings to src/i18n.cpp.

bool printLocalePrefersLetter() {
    // ImperialUSSystem only. ImperialUKSystem means A4 paper, as LOCALE_IMEASURE's '0' does
    // for a UK locale on the Windows arm.
    return QLocale::system().measurementSystem() == QLocale::ImperialUSSystem;
}

// A QtRenderTarget over a 1x1 image, created on first use and DELIBERATELY NEVER DESTROYED.
// QtRenderTarget begins a QPainter in its constructor and ends it in its destructor
// (include/render_qt.h). A function-local static would end that painter during static
// destruction, after QApplication is gone, and an exit-time Qt call is not worth the bytes it
// saves. The print code uses it only as a factory: rasterizeToImage ignores its target on
// this arm, and CreateBitmap builds a QImage without touching the painter. So the image
// behind it is never drawn on, and its size does not matter.
tinta::RenderTarget* printOffscreenTarget() {
    static QtRenderTarget* const target = [] {
        auto* device = new QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        device->fill(Qt::transparent);
        return new QtRenderTarget(*device);
    }();
    return target;
}

bool printDocument(App& app) {
    std::fprintf(stderr, "tinta: printing is not available in the Qt build yet -- "
                         "src/print_qt.cpp has no QPrinter arm\n");
    signalPush(app, SIG_WARN, SIGI_WARNING, L"Printing isn't available in the Qt build yet");
    return false;
}

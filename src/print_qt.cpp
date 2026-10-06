#include "print_pages.h"
#include "signals.h"

#include <QLocale>

#include <cstdio>

// THE QT ARM OF src/print.cpp's TWO BACKEND HOOKS. Until 2026-10-06 this file defined seven
// print symbols, and every one refused: preview, PDF export and the link peek included. Those
// now live in print.cpp itself, which compiles off Windows. What is left is the two questions
// only a platform can answer: printDocument (include/print.h) and the paper locale
// (src/print_pages.h).
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

bool printDocument(App& app) {
    std::fprintf(stderr, "tinta: printing is not available in the Qt build yet -- "
                         "src/print_qt.cpp has no QPrinter arm\n");
    signalPush(app, SIG_WARN, SIGI_WARNING, L"Printing isn't available in the Qt build yet");
    return false;
}

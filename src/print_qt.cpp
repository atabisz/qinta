#include "print.h"
#include "export.h"
#include "signals.h"

#include <cstdio>

// THE QT ARM OF src/print.cpp, AND IT PRINTS NOTHING. Every function here refuses, visibly,
// and that is the whole of this file -- a stated gap, not a port.
//
// Why it exists anyway: a link fixpoint over the manifest on 2026-09-24 measured that
// src/input.cpp needs six of these symbols (src/editor.cpp one of the same six) and
// src/export_html.cpp the seventh, and that
// no compile-only file links off Windows while any of them is undefined. print.cpp's other
// two, printDocument and printDebugPages, are called only from print.cpp itself and the
// Windows-only src/main_d2d.cpp, so they have no arm here. print.cpp stays the Windows arm,
// unchanged: it holds
// ID2D1PrintControl, DEVMODE and the printer dialog, and a real QPrinter port waits on a
// CJK print fixture that can tell a correct page from a plausible one.
//
// A REFUSAL HERE IS A CHIP AND A LINE ON stderr, the same two channels src/main_qt.cpp's
// refused flags and the rest of the app use. The chip text is ENGLISH ONLY: it is not an
// i18n key, because translating a message that exists to be deleted would add four strings
// to src/i18n.cpp for a file whose goal is to stop existing.

namespace {

void refusePrint(App& app, const char* what) {
    std::fprintf(stderr, "tinta: %s is not available in the Qt build yet -- src/print.cpp "
                         "has no Qt arm\n", what);
    signalPush(app, SIG_WARN, SIGI_WARNING, L"Printing isn't available in the Qt build yet");
}

}  // namespace

void openPrintPreview(App& app, HWND) { refusePrint(app, "print preview"); }

// Silent on purpose, and unreachable today: every caller of these three (src/input.cpp's
// Escape key and Cancel/paper hit tests) sits behind app.showPrintPreview, and the refusal
// above never sets it. They exist because the linker resolves a call it cannot see is dead.
void closePrintPreview(App&, HWND) {}
void printPreviewSetPage(App&, int) {}
void printPreviewSetFormat(App&, int, bool) {}

void printPreviewConfirm(App& app, HWND) { refusePrint(app, "printing"); }

// The link peek draws a rendered miniature of the linked file. A null bitmap is the
// caller's existing "no peek" path (src/input.cpp returns before showing the panel), so this
// refusal is silent: hovering a link is not a request to print.
//
// THE BODY OF THE WINDOWS ARM IS ALREADY PORTABLE -- include/print.h says so of this
// function in those words -- so this is the cheapest of the seven to make real: move that
// body into a shared file and delete this definition. Not done here because the approved
// scope left print.cpp untouched.
std::shared_ptr<tinta::Bitmap> renderPeekBitmap(App&, const std::wstring&, float,
                                                float&) {
    return nullptr;
}

// Returns false, and the caller then pushes its own generic "Save failed" chip beside this
// one -- which names the wrong cause (a locked or read-only file). That double chip is a
// known wart of refusing through a bool, recorded rather than fixed: the fix is a real PDF
// path, and src/pdf_export_qt.cpp already holds the seam's Qt half for it.
bool exportPdfFile(App& app, const std::wstring&) {
    refusePrint(app, "PDF export");
    return false;
}

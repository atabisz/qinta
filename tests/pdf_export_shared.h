#ifndef TINTA_PDF_EXPORT_SHARED_H
#define TINTA_PDF_EXPORT_SHARED_H

// WINDOWS.H FIRST, AND FOR THE REASON tests/render_api_shared.h GIVES AT LENGTH.
// pdf_export.h includes render_api.h, which hard-errors on Windows unless the DrawText
// macro is already in scope, and this file is compiled into both a Windows and a
// non-Windows target from one source. Same exception, same reason, and the comment is
// cited rather than copied.
#ifdef _WIN32
#include <windows.h>
#endif

#include "pdf_export.h"

#include <string>

// ONE ASSERTION SET, BOTH PDF BACKENDS.
//
// include/pdf_export.h has had exactly one implementation since it landed, and its own
// slice's ISA called that debt by name out of CMakeLists.txt:2658: a seam with one
// implementation is indirection, not abstraction. Two per-arm test files would only
// show that each arm is self-consistent. What is worth catching is the two DISAGREEING
// about the contract -- a nullptr one arm returns and the other does not, a page count
// that is right in one producer's bytes and off by one in the other's, a painter called
// twice for one page.
//
// WHAT CANNOT BE ASSERTED HERE, stated so nobody adds it: the two arms' BYTES. The XPS
// route and QPdfWriter are different PDF producers, so identical drawing yields
// unrelated files and no cross-arm diff means anything. The Windows arm's output is
// held to its own recorded baseline instead, outside this file. Everything below is a
// property of the CONTRACT, which is the part both arms really do share.
//
// A backend joins by implementing PdfHarness. Nothing in the assertion body names Qt,
// Direct2D, QPdfWriter, QPainter, ID2D1 or XpsPageEmitter.

class PdfHarness {
public:
    virtual ~PdfHarness() = default;

    // A live render target to hand to beginPdfDocument. One arm needs a device it can
    // create a print control on; the other ignores the argument entirely. The harness
    // supplies whatever its own backend considers live, and no assertion below knows
    // which of those two situations it is in.
    virtual tinta::RenderTarget& target() = 0;

    // A directory that exists and is writable, where the assertions may create and
    // delete files. Supplied by the harness rather than derived here because the two
    // targets run under different working directories.
    virtual std::wstring scratchDir() = 0;

    // Backend name for the summary line, so a green run says WHICH backend was green.
    virtual const char* name() const = 0;
};

// Runs every assertion against the harness. Returns the number that FAILED, printing a
// line per failure plus a summary. The ASSERTION COUNT is printed too, and the two
// targets' counts are compared by the run's ISA: an arm silently running fewer
// assertions than the other must not be able to read as a pass.
int runPdfExportAssertions(PdfHarness& harness);

// Pages counted out of a PDF file's own bytes, or -1 if the file cannot be read.
//
// EXPOSED BECAUSE IT IS AN INSTRUMENT AND INSTRUMENTS GET CHECKED. This is not a PDF
// parser: it counts `/Type /Page` tokens and rejects `/Pages`, which is sound only for
// producers that write page objects uncompressed. Both of this tree's producers do, and
// that is a measured claim rather than an assumption -- it is cross-checked against the
// two page counts the PDF baseline already records for real documents (3 and 140) and
// against an independent route to the same pagination (`--printpages`).
int countPdfPages(const std::wstring& path);

#endif  // TINTA_PDF_EXPORT_SHARED_H

// The PDF seam, asserted against any backend, from the BYTES IT WROTE.
//
// WHY THE BYTES AND NOT THE SINK'S OWN ANSWERS
//
// Every interesting failure of a PDF backend returns true. `addPage` can succeed three
// times and produce a one-page document, because one of the two arms holds a single
// painter across the whole document and ending it early truncates the file; `finish()`
// can succeed having written a header and no trailer; a page-size conversion can be off
// by 96/72 and nothing in the sink's return values notices. So the sink's bookkeeping is
// not read anywhere below: the page count, the size and the magic bytes all come off
// disk, and the only thing the sink's own answers are used for is deciding whether the
// document was worth reading.
//
// WHAT IS NOT ASSERTED HERE, and each of these was considered and rejected rather than
// overlooked:
//
//   * The two arms' BYTES against each other. Different PDF producers; identical drawing
//     yields unrelated files. The Windows arm is held to its own recorded baseline
//     outside this file.
//   * `finish()` on a document with ZERO pages. The seam does not say, its only caller
//     short-circuits on `pages > 0` before ever asking, and the two arms answer
//     differently -- one will happily close an empty document, the other has nothing
//     open to close. Asserting either answer would invent a contract out of one
//     implementation's incidental behaviour.
//   * A zero or negative page size. One arm rejects it, the other forwards it; same
//     reason as above, and the seam is silent.
//   * Whether a FILE survives a failed open. include/pdf_export.h documents that one arm
//     leaves an empty file behind deliberately and the other leaves none. The seam's
//     promise is about the return value, so that is what ISC-8 reads.
//   * That the painter is handed a non-null brush. The seam says it MAY be null. Both
//     arms pass one today, so the reading is printed rather than checked -- a check here
//     would narrow the seam by accident.
//
// The arms are "one arm" and "the other" throughout, above and below, and no producer,
// type or API belonging to either is named anywhere in this file -- which is the property
// that lets both targets compile it, and is measured by grep rather than trusted here.
//
// THAT SENTENCE USED TO ENUMERATE THE SEVEN NAMES IT FORBADE, and enumerating them is how
// it became false about itself: the grep for those names hit the disclaimer, and it hit
// two bullets above that had named the arms by backend while claiming none did. A comment
// asserting an absence cannot be its own witness if it spells the absence out.

#include "pdf_export_shared.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

// std::filesystem::path is what carries a wide path portably into a stream here -- the
// same spelling tests/render_api_shared.cpp settled on, and deliberately NOT an fstream
// constructed from a std::wstring, which MSVC accepts as an extension and libstdc++
// rejects outright.
std::vector<unsigned char> readAll(const std::wstring& path) {
    const std::filesystem::path fsPath(path);
    std::ifstream in(fsPath, std::ios::binary);
    if (!in) return {};
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

std::wstring join(const std::wstring& dir, const wchar_t* leaf) {
    std::filesystem::path p(dir);
    p /= leaf;
    return p.wstring();
}

bool isPdfWhitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0;
}

// A regular character in a PDF name, which is what distinguishes `/Page` from `/Pages`.
// Deliberately generous: anything that is neither whitespace nor a delimiter continues
// the name, so a name this test has never seen cannot be truncated into a match.
bool continuesName(unsigned char c) {
    if (isPdfWhitespace(c)) return false;
    return std::strchr("()<>[]{}/%", c) == nullptr;
}

}  // namespace

int countPdfPages(const std::wstring& path) {
    const std::vector<unsigned char> bytes = readAll(path);
    if (bytes.empty()) return -1;

    const std::string needle = "/Type";
    int pages = 0;
    for (size_t i = 0; i + needle.size() < bytes.size(); i++) {
        if (std::memcmp(&bytes[i], needle.data(), needle.size()) != 0) continue;
        size_t j = i + needle.size();
        while (j < bytes.size() && isPdfWhitespace(bytes[j])) j++;
        // `/Type/Page` with no separator is as legal as `/Type /Page`, and one producer
        // writing it the tight way would otherwise read as a zero-page document.
        if (j + 5 > bytes.size()) continue;
        if (std::memcmp(&bytes[j], "/Page", 5) != 0) continue;
        // THE WHOLE DIFFICULTY OF THIS FUNCTION IS THIS LINE. `/Pages` is the page TREE
        // node, one per document, and counting it would make every document read one page
        // heavy -- which for a single-page document is a 100% error that still looks like
        // a plausible number.
        if (j + 5 < bytes.size() && continuesName(bytes[j + 5])) continue;
        pages++;
    }
    return pages;
}

namespace {

// The first page's width in POINTS, or -1 if no MediaBox was found. Points because that
// is the unit a PDF is written in whatever unit the caller drew in, so this is the one
// reading that crosses the seam's DIP boundary and can catch a conversion error.
double firstMediaBoxWidthPoints(const std::wstring& path) {
    const std::vector<unsigned char> bytes = readAll(path);
    if (bytes.empty()) return -1.0;
    const std::string needle = "/MediaBox";
    for (size_t i = 0; i + needle.size() < bytes.size(); i++) {
        if (std::memcmp(&bytes[i], needle.data(), needle.size()) != 0) continue;
        size_t j = i + needle.size();
        while (j < bytes.size() && (isPdfWhitespace(bytes[j]) || bytes[j] == '[')) j++;
        const size_t span = (bytes.size() - j < 128u) ? bytes.size() - j : 128u;
        const std::string text(reinterpret_cast<const char*>(&bytes[j]), span);
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (std::sscanf(text.c_str(), "%lf %lf %lf %lf", &x0, &y0, &x1, &y1) == 4) {
            return x1 - x0;
        }
        return -1.0;
    }
    return -1.0;
}

// One document written through the seam, plus everything observable about the WRITING of
// it. The file is left on disk for the caller to read; the sink is destroyed before this
// returns, which is load-bearing rather than tidy -- one arm completes its file when the
// sink dies, so reading the bytes while the sink is alive reads a truncated document.
struct DocResult {
    bool sinkCreated = false;
    int addPageCalls = 0;
    int addPageFalse = 0;
    int painterCalls = 0;
    int nullBrushes = 0;
    bool finished = false;
    std::wstring path;
    uintmax_t size = 0;
    bool exists = false;
};

DocResult writeDocument(PdfHarness& harness, const wchar_t* leaf, int pageCount,
                        float pageW, float pageH, int rectsPerPage) {
    DocResult r;
    r.path = join(harness.scratchDir(), leaf);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(r.path), ec);

    {
        std::unique_ptr<tinta::PdfPageSink> sink =
            tinta::beginPdfDocument(harness.target(), r.path, L"pdf export contract");
        r.sinkCreated = sink != nullptr;
        if (sink) {
            for (int page = 0; page < pageCount; page++) {
                r.addPageCalls++;
                const bool ok = sink->addPage(
                    pageW, pageH, [&](tinta::RenderTarget& target, tinta::Brush* brush) {
                        r.painterCalls++;
                        if (!brush) {
                            r.nullBrushes++;
                            return;  // the seam says a painter handed null draws nothing
                        }
                        // Distinct rectangles rather than one repeated: a producer that
                        // compressed the content stream could collapse a repeat, and the
                        // size relation below would stop discriminating.
                        for (int i = 0; i < rectsPerPage; i++) {
                            const float x = 10.0f + static_cast<float>(i % 20) * 7.0f;
                            const float y = 10.0f + static_cast<float>(i / 20) * 11.0f;
                            target.FillRectangle(tinta::rectF(x, y, x + 5.0f, y + 6.0f),
                                                 brush);
                        }
                    });
                if (!ok) r.addPageFalse++;
            }
            r.finished = sink->finish();
        }
    }  // the sink dies HERE, and one arm's file is not complete until it has

    std::error_code sizeEc;
    const uintmax_t size = std::filesystem::file_size(std::filesystem::path(r.path), sizeEc);
    r.exists = !sizeEc;
    r.size = r.exists ? size : 0;
    return r;
}

bool startsWithPdfMagic(const std::wstring& path) {
    const std::vector<unsigned char> bytes = readAll(path);
    if (bytes.size() < 5) return false;
    return std::memcmp(bytes.data(), "%PDF-", 5) == 0;
}

}  // namespace

int runPdfExportAssertions(PdfHarness& harness) {
    failures = 0;
    checks = 0;

    // Letter at 96 DIPs to the inch, which is the unit src/print.cpp's PageGeometry hands
    // the seam. The wide document is exactly twice as wide, so the MediaBox relation below
    // needs no absolute number and no knowledge of either producer's rounding.
    const float kPageW = 816.0f;
    const float kPageH = 1056.0f;

    // --- CONTROL: is the scratch directory writable at all? -------------------
    //
    // Without this, ISC-8's "an unwritable path returns nullptr" would be satisfied by a
    // harness whose scratch directory does not exist -- the assertion would pass for the
    // wrong reason and the happy-path failures would look like backend bugs.
    {
        const std::wstring probe = join(harness.scratchDir(), L"tinta-pdf-probe.txt");
        bool wrote = false;
        {
            std::ofstream out(std::filesystem::path(probe), std::ios::binary);
            if (out) {
                out << "probe";
                wrote = out.good();
            }
        }
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(std::filesystem::path(probe), ec);
        check(wrote && !ec && size == 5,
              "CONTROL: the scratch directory is writable -- a read-only one would make "
              "every failure below look like a backend defect");
        std::filesystem::remove(std::filesystem::path(probe), ec);
    }

    // --- The happy path, three pages, and the file it left behind --------------
    DocResult three = writeDocument(harness, L"tinta-pdf-three.pdf", 3, kPageW, kPageH, 60);
    {
        check(three.sinkCreated,
              "beginPdfDocument returns a sink for a writable path");
        check(three.addPageFalse == 0, "every addPage call reports success");
        check(three.painterCalls == three.addPageCalls,
              "the painter is called exactly once per addPage -- not zero times, and not "
              "twice for one page");
        check(three.finished, "finish() succeeds on a document with pages");
        check(three.exists, "a file exists at the path afterwards");
        check(three.size > 0, "and it is not empty");
        check(startsWithPdfMagic(three.path),
              "the file begins %PDF- -- so what was written is a PDF and not a print "
              "spool format, an error page, or nothing");

        // ISC-6, AND THE REASON IT SAYS "FROM THE BYTES". The sink counted three
        // addPage calls; that is a statement about the loop above, not about the file.
        const int counted = countPdfPages(three.path);
        check(counted == 3,
              "the PDF's own bytes carry three page objects -- one arm holds a single "
              "painter across the document, and ending it early would leave one page "
              "here while every return value above still said true");
        std::printf("       three-page document: %d pages counted, %llu bytes, backend=%s\n",
                    counted, static_cast<unsigned long long>(three.size), harness.name());
    }

    // --- One page, which is what makes the page count a measurement ------------
    //
    // A counter hardcoded to 3, or one that counted the page TREE node instead of the
    // pages, passes everything above. This is the cheapest thing that separates "the
    // instrument read three" from "the instrument says three".
    DocResult one = writeDocument(harness, L"tinta-pdf-one.pdf", 1, kPageW, kPageH, 60);
    {
        check(one.sinkCreated && one.finished,
              "CONTROL: the single-page document was written, so the count below is of a "
              "real file");
        const int counted = countPdfPages(one.path);
        check(counted == 1,
              "a one-page document carries exactly ONE page object -- a counter that "
              "included the page-tree node would read 2 here and 4 above");
    }

    // --- Does the DRAWING reach the file? -------------------------------------
    //
    // Everything above is satisfied by a backend that writes three empty pages and
    // ignores the painter entirely -- the magic bytes, the page count and the return
    // values are all unaffected by whether a single rectangle arrived. The painter is
    // called with a live target, so the only reading that distinguishes those two is the
    // file's own size.
    DocResult blank = writeDocument(harness, L"tinta-pdf-blank.pdf", 3, kPageW, kPageH, 0);
    {
        check(blank.sinkCreated && blank.finished && blank.size > 0,
              "CONTROL: a document whose painter drew nothing is still written");
        check(countPdfPages(blank.path) == 3,
              "CONTROL: and still has three pages, so the comparison below is between two "
              "documents of the same shape");
        check(three.size > blank.size,
              "a document with 180 rectangles in it is LARGER than the same three pages "
              "with nothing drawn -- so the painter's output reaches the file");
        std::printf("       drawn %llu bytes vs blank %llu bytes, painter brush was null "
                    "%d time(s) (backend=%s)\n",
                    static_cast<unsigned long long>(three.size),
                    static_cast<unsigned long long>(blank.size), three.nullBrushes,
                    harness.name());
    }

    // --- The page SIZE crosses the seam, and it crosses a unit boundary --------
    //
    // The seam speaks DIPs and a PDF is written in points, so there is a 96/72 conversion
    // in at least one arm and nothing else in this file would notice it being wrong: a
    // document at 133% of its intended size has the right page count, the right magic
    // bytes and a plausible file size.
    //
    // The assertion is RELATIONAL for the same reason the drawing-seam suite's text
    // assertions are: a literal 612 would be a statement about one producer's rounding.
    // Twice as wide has to read twice as wide, whatever the unit.
    DocResult wide =
        writeDocument(harness, L"tinta-pdf-wide.pdf", 1, kPageW * 2.0f, kPageH, 60);
    {
        const double narrowWidth = firstMediaBoxWidthPoints(one.path);
        const double wideWidth = firstMediaBoxWidthPoints(wide.path);
        check(wide.sinkCreated && wide.finished,
              "CONTROL: the double-width document was written");
        check(narrowWidth > 0.0 && wideWidth > 0.0,
              "CONTROL: a MediaBox was found in both documents -- without one this "
              "reading is undecidable rather than negative");
        // A tolerance, not equality: the two producers are free to round a page box to
        // an integer number of points, and 816 DIPs is 612 points exactly only because
        // of the numbers chosen above.
        check(wideWidth > narrowWidth * 1.9 && wideWidth < narrowWidth * 2.1,
              "a page twice as wide in DIPs is twice as wide in the PDF -- a dropped or "
              "doubled 96/72 conversion fails exactly here");
        std::printf("       MediaBox widths: narrow %.2f pt, wide %.2f pt (backend=%s)\n",
                    narrowWidth, wideWidth, harness.name());
    }

    // --- ISC-8, anti: an unwritable destination ------------------------------
    //
    // A directory that does not exist, which is the portable spelling of "unwritable" --
    // a read-only path would need a permission model the two hosts do not share. What is
    // asserted is the RETURN VALUE and nothing else: the two arms differ on whether a
    // file is left behind, deliberately and documented, so a filesystem assertion here
    // would be a statement about one of them.
    {
        const std::wstring bad =
            join(join(harness.scratchDir(), L"tinta-no-such-directory-a8f3"), L"x.pdf");
        std::unique_ptr<tinta::PdfPageSink> sink =
            tinta::beginPdfDocument(harness.target(), bad, L"unwritable");
        check(sink == nullptr,
              "an unwritable destination returns a NULL sink -- not a live sink that "
              "silently drops every page it is given");
    }

    std::error_code ec;
    for (const std::wstring* p : {&three.path, &one.path, &blank.path, &wide.path}) {
        std::filesystem::remove(std::filesystem::path(*p), ec);
    }

    std::printf("%s%d pdf-export assertions run, %d failed (backend=%s)\n",
                failures == 0 ? "all " : "", checks, failures, harness.name());
    return failures;
}

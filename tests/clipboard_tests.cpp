// include/clipboard.h -- the clipboard seam's three line-ending transformations, and a
// round trip of both TEXT and IMAGE through whichever backend this build selected.
//
// THE IMAGE ROUND TRIP ASSERTS A PIXEL AND NOT A SIZE, and that is the one design note
// worth reading before the code. src/export_docx.cpp used to carry a warning about
// handing alpha-less bytes to a premultiplied-BGRA contract: it "would encode a fully
// transparent PNG -- a plausible-looking file with nothing in it". Every check a lazier
// test would make passes on that file. It is the right length, the dimensions are right,
// the ok flag is true, and a PNG encoder accepts it. So the assertions below read the
// bytes: every alpha byte, and every colour triple at its own coordinate.
//
// WHY THE TRANSFORMATIONS ARE THE PART WORTH ASSERTING, which is file_dialog.h's
// argument about its filter encoders applied to a different seam. clipboardText() and
// setClipboardText() can be asserted to say very little: a round trip, and that an
// empty write is refused. The part with decisions in it is what happens to a carriage
// return, and the census found THREE sites that answer that differently. Those three
// answers are inline in the header, so the assertions below do not call either
// backend and hold whichever one CMake picked -- a Win32 stub and a Qt stub would both
// have to produce the same results here, because neither is involved.
//
// THE BACKEND IS STILL LINKED, for two reasons that are not "to test it". First, so a
// non-Windows build COMPILES src/clipboard_qt.cpp and the file therefore appears in
// tests/portable-sources.txt -- the same argument CMakeLists.txt makes for
// ${PLATFORM_FILE_DIALOG_SOURCE} in file_dialog_tests. Second, so the round trip at
// the bottom can measure the one property the seam cannot promise: whether the
// platform normalizes a CRLF before the seam ever sees it.
//
// THE ROUND TRIP SAVES AND RESTORES WHAT WAS ON THE CLIPBOARD. A test that leaves a
// sentinel in the user's clipboard has silently destroyed something they copied, and
// the failure is invisible until they paste. tests/file_path_tests.cpp does exactly
// that today and this file does not copy the habit.
//
// POSITIVE CONTROL: `--control` perturbs every EXPECTED value, so all of the
// transformation checks must fail. A run without it proves only that the assertions
// executed.

#include "clipboard.h"

#include "image_api.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#ifdef TINTA_CLIPBOARD_BACKEND_QT
#include <QGuiApplication>
#endif
// The raw-DIB producer below talks to the Windows clipboard directly, on purpose, whichever
// backend the seam uses. Since 2026-10-07 that backend is Qt on Windows too.
#ifdef _WIN32
#include <windows.h>

#include <cstring>
#endif

namespace {

int failures = 0;
int checks = 0;
bool control = false;

// Perturbs the expectation, not the input: the transformations are exercised on
// exactly the same data in both modes, so a control failure means the assertion is
// anchored to a specific result rather than to anything about the call.
std::wstring expect(const std::wstring& value) {
    return control ? value + L"\x1" : value;
}

void eq(const std::wstring& actual, const std::wstring& wanted, const char* name) {
    checks++;
    if (actual == wanted) {
        std::cout << "  ok   " << name << "\n";
        return;
    }
    failures++;
    std::cout << "  FAIL " << name << "\n";
}

// A CONTRACT rather than a computed expectation: something that must hold in both
// modes, so there is nothing for --control to perturb. Counted separately, because a
// control whose threshold is "about this many should survive" cannot distinguish a
// loose assertion from a missing one. The threshold below is this counter, exactly.
int unperturbed = 0;

void contract(bool cond, const char* name) {
    checks++;
    unperturbed++;
    if (cond) {
        std::cout << "  ok   " << name << "  (contract, not perturbed)\n";
        return;
    }
    failures++;
    std::cout << "  FAIL " << name << "  (contract, not perturbed)\n";
}

// --- annotations.cpp:361's rule: CRLF -> LF, and a LONE CR SURVIVES -------------
void testNormalizeCrLfOnly() {
    std::cout << "-- normalizeCrLfOnly (annotations.cpp's rule)\n";
    using platform::normalizeCrLfOnly;
    eq(normalizeCrLfOnly(L""), expect(L""), "empty stays empty");
    eq(normalizeCrLfOnly(L"plain"), expect(L"plain"), "text with no line ending is untouched");
    eq(normalizeCrLfOnly(L"a\r\nb"), expect(L"a\nb"), "CRLF becomes LF");
    eq(normalizeCrLfOnly(L"a\r\nb\r\nc"), expect(L"a\nb\nc"), "every CRLF becomes LF");
    eq(normalizeCrLfOnly(L"a\nb"), expect(L"a\nb"), "a bare LF is left alone");

    // THE DEFINING CASE. This is the behaviour that made this a separate function from
    // normalizeNewlines instead of a call to it: a carriage return with no newline
    // after it reaches annotations.cpp's note buffer as a carriage return. Asserting
    // it means a later tidy-up that unifies the two reads fails HERE, loudly, rather
    // than changing what a paste produces at one of the two sites.
    eq(normalizeCrLfOnly(L"a\rb"), expect(L"a\rb"), "a LONE CR survives -- the weaker rule");
    eq(normalizeCrLfOnly(L"a\r"), expect(L"a\r"), "a trailing lone CR survives");

    // \n\r is not \r\n. Worth its own line because the replace loop scans for the pair
    // in one order and a reader can carry the two-character sequence in their head
    // without its direction.
    eq(normalizeCrLfOnly(L"a\n\rb"), expect(L"a\n\rb"), "LF-then-CR is not the CRLF pair");
    eq(normalizeCrLfOnly(L"a\r\r\nb"), expect(L"a\r\nb"), "CR CR LF loses only the pair");
}

// --- editor.cpp:572's rule: every CR -> LF, swallowing a following LF -----------
void testNormalizeNewlines() {
    std::cout << "-- normalizeNewlines (editor.cpp's rule)\n";
    using platform::normalizeNewlines;
    eq(normalizeNewlines(L""), expect(L""), "empty stays empty");
    eq(normalizeNewlines(L"plain"), expect(L"plain"), "text with no line ending is untouched");
    eq(normalizeNewlines(L"a\r\nb"), expect(L"a\nb"), "CRLF becomes one LF");
    eq(normalizeNewlines(L"a\nb"), expect(L"a\nb"), "a bare LF is left alone");
    eq(normalizeNewlines(L"a\rb"), expect(L"a\nb"), "a LONE CR becomes LF -- the stronger rule");
    eq(normalizeNewlines(L"a\r"), expect(L"a\n"), "a trailing lone CR becomes LF");
    eq(normalizeNewlines(L"a\r\r\nb"), expect(L"a\n\nb"), "CR CR LF becomes two LFs");
    eq(normalizeNewlines(L"a\n\rb"), expect(L"a\n\nb"), "LF-then-CR becomes two LFs");
    eq(normalizeNewlines(L"a\r\n\r\nb"), expect(L"a\n\nb"), "a blank CRLF line survives as a blank line");

    // Idempotence, which is the property that makes the header's claim about Qt
    // honest: if QClipboard has already normalized the text, applying this to the
    // result changes nothing, so this site cannot tell the two platforms apart.
    eq(normalizeNewlines(normalizeNewlines(L"a\r\nb\rc")), expect(L"a\nb\nc"),
       "idempotent -- so pre-normalized input is indistinguishable");
}

// --- input.cpp's paste-into-single-line rule: CR, LF and TAB each -> ONE space -------------------
void testFlattenToSingleLine() {
    std::cout << "-- flattenToSingleLine (input.cpp's and editor.cpp:2286's rule)\n";
    using platform::flattenToSingleLine;
    eq(flattenToSingleLine(L""), expect(L""), "empty stays empty");
    eq(flattenToSingleLine(L"plain"), expect(L"plain"), "text with no whitespace is untouched");
    eq(flattenToSingleLine(L"a\tb"), expect(L"a b"), "TAB becomes a space");
    eq(flattenToSingleLine(L"a\nb"), expect(L"a b"), "LF becomes a space");
    eq(flattenToSingleLine(L"a\rb"), expect(L"a b"), "CR becomes a space");

    // THE CASE THAT DECIDED THE SEAM'S SIGNATURE. A CRLF is two characters and this
    // maps each of them, so it produces TWO spaces. The first design normalized inside
    // clipboardText(), which would have removed one of the two before this ever counted
    // them, silently turning a pasted CRLF into one space at the one site whose
    // transformation is not a normalization. That is why the seam returns raw text.
    eq(flattenToSingleLine(L"a\r\nb"), expect(L"a  b"), "CRLF becomes TWO spaces -- not one");
    contract(flattenToSingleLine(L"a\r\nb").size() == 4, "flatten never changes the length");
    eq(flattenToSingleLine(L"a\r\n\r\nb"), expect(L"a    b"), "a blank CRLF line becomes four spaces");

    // Ordinary spaces are NOT collapsed. This is a flatten, not a whitespace
    // normalizer, and a reader who expects collapseWs would be surprised.
    eq(flattenToSingleLine(L"a   b"), expect(L"a   b"), "existing spaces are not collapsed");
}

// --- the divergence, asserted as a difference rather than two facts ------------
void testTheThreeDisagree() {
    std::cout << "-- the three rules disagree, and the seam carries it\n";
    const std::wstring loneCr = L"one\rtwo";

    // Not eq() against a literal: the point is that the two functions differ from each
    // other on the same input, which a pair of independent literal assertions states
    // but does not tie together. If someone unifies the two rules, both assertions
    // above still pass in whichever direction they unified -- this one cannot.
    contract(platform::normalizeCrLfOnly(loneCr) != platform::normalizeNewlines(loneCr),
          "the two read rules give DIFFERENT answers for a lone CR");
    contract(platform::normalizeCrLfOnly(L"a\r\nb") == platform::normalizeNewlines(L"a\r\nb"),
          "...and the SAME answer for a CRLF, so the divergence is narrow");
    contract(platform::flattenToSingleLine(L"a\nb") != platform::normalizeNewlines(L"a\nb"),
          "the flatten is not a normalization at all");
}

// --- the image half: a fixture built so a flip or a swap cannot hide ------------

constexpr unsigned kImgW = 4;
constexpr unsigned kImgH = 3;

// EVERY CHANNEL DEPENDS ON BOTH COORDINATES, which is the whole reason this is not a
// solid colour or a gradient in one direction. A solid fixture round-trips through a
// backend that flipped every row, mirrored every column and swapped red for blue. These
// three formulas make each of those three mistakes change at least one byte, and the
// assertions below then only have to compare.
uint8_t blueAt(unsigned x, unsigned y) { return (uint8_t)(10 + x * 20 + y * 3); }
uint8_t greenAt(unsigned x, unsigned y) { return (uint8_t)(40 + y * 50 + x * 2); }
uint8_t redAt(unsigned x, unsigned y) { return (uint8_t)(200 - x * 10 - y * 7); }

tinta::DecodedImage makeTestImage() {
    tinta::DecodedImage img;
    img.width = kImgW;
    img.height = kImgH;
    img.pixels.resize((size_t)kImgW * kImgH * 4);
    for (unsigned y = 0; y < kImgH; y++) {
        for (unsigned x = 0; x < kImgW; x++) {
            uint8_t* p = img.pixels.data() + ((size_t)y * kImgW + x) * 4;
            p[0] = blueAt(x, y);
            p[1] = greenAt(x, y);
            p[2] = redAt(x, y);
            // Alpha 255 throughout, which makes every colour above a VALID premultiplied
            // value -- at full opacity the premultiplied and straight representations of
            // a pixel are the same bytes, so the fixture cannot be rejected as impossible.
            p[3] = 255;
        }
    }
    img.ok = true;
    return img;
}

#ifdef _WIN32
// A PRODUCER THE SEAM NEVER IS: a raw CF_DIB carrying the fixture's colours with ZERO in
// every fourth byte, published without going through platform::setClipboardImage. It
// exists because the round trip alone cannot measure what the read's forced alpha is for.
//
// MEASURED 2026-09-19, and it is why this function was written rather than assumed: with
// the forced-alpha line deleted from src/clipboard_win32.cpp, the round trip below stayed
// GREEN -- all 44 checks, including the alpha contract. Everything the seam writes carries
// alpha 255, so GetDIBits hands 255 back, and "the read forces opaque" and "the read
// preserved what the write put there" are indistinguishable through the seam's own front
// door. A sentinel confirmed the assertion is wired (forcing 254 fails it by name), so the
// gap was in the INPUT, not the check.
//
// What this supplies is the case the force actually defends: a screenshot tool, Paint, or
// any other application publishing a 32bpp BI_RGB DIB, where the fourth byte is documented
// as undefined and nothing had a reason to write 255 into it.
//
// The open retries for the same measured reason src/clipboard_win32.cpp's writes did. That
// file was deleted on 2026-10-07; the loop stays here because another process holding the
// clipboard is still the race it answers.
bool publishZeroAlphaDib() {
    const size_t stride = (size_t)kImgW * 4;
    const size_t bytes = stride * kImgH;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
    if (!dib) return false;
    BYTE* out = (BYTE*)GlobalLock(dib);
    if (!out) {
        GlobalFree(dib);
        return false;
    }
    BITMAPINFOHEADER hdr{};
    hdr.biSize = sizeof(hdr);
    hdr.biWidth = (LONG)kImgW;
    hdr.biHeight = (LONG)kImgH;  // positive: bottom-up, so row 0 of the DIB is the LAST row
    hdr.biPlanes = 1;
    hdr.biBitCount = 32;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage = (DWORD)bytes;
    std::memcpy(out, &hdr, sizeof(hdr));
    for (unsigned y = 0; y < kImgH; y++) {
        BYTE* row = out + sizeof(hdr) + (size_t)(kImgH - 1 - y) * stride;
        for (unsigned x = 0; x < kImgW; x++) {
            row[x * 4 + 0] = blueAt(x, y);
            row[x * 4 + 1] = greenAt(x, y);
            row[x * 4 + 2] = redAt(x, y);
            row[x * 4 + 3] = 0;  // the whole point of this fixture
        }
    }
    GlobalUnlock(dib);

    bool opened = false;
    for (int attempt = 0; attempt < 10 && !opened; attempt++) {
        opened = OpenClipboard(nullptr) != 0;
        if (!opened) Sleep(10);
    }
    if (!opened) {
        GlobalFree(dib);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
    if (!ok) GlobalFree(dib);  // ownership transfers only on success
    CloseClipboard();
    return ok;
}
#endif

void testImageSeam() {
    std::cout << "-- the image half of the seam, through whichever backend this build selected\n";

    // Refused before anything is touched, same shape as the empty-text contract: an
    // ok == false DecodedImage has unspecified fields by image_api.h's own documentation,
    // so a backend that read them would be reading uninitialized memory.
    contract(!platform::setClipboardImage(tinta::DecodedImage(), std::string()),
             "an image with ok == false is refused");

    const tinta::DecodedImage wrote = makeTestImage();

    // THE FIXTURE'S OWN CONTROL, asserted before the round trip rather than assumed by
    // it. If the top-left and bottom-left pixels were equal, the row-order assertion
    // further down could not fail no matter what the backend did, and it would read green
    // forever. This is the same trap as asserting two readings of one surface against
    // each other.
    contract(blueAt(0, 0) != blueAt(0, kImgH - 1) && greenAt(0, 0) != greenAt(0, kImgH - 1),
             "the fixture's top-left and bottom-left differ, so a row flip is detectable");

    if (!platform::setClipboardImage(wrote, std::string())) {
        // Not a failure, for the same reason the text round trip's skip is not: a build
        // with no application instance, or a clipboard another process is holding open,
        // reaches here without anything being wrong with the seam.
        std::cout << "  SKIP  the image round trip -- setClipboardImage refused"
                     " (no application instance, or the clipboard was unavailable)\n";
        return;
    }

    contract(platform::clipboardHasImage(),
             "the availability bit sees the image the seam just wrote");

    const tinta::DecodedImage back = platform::clipboardImage();
    contract(back.ok, "an image written through the seam reads back with ok == true");
    if (!back.ok) return;
    contract(back.width == kImgW && back.height == kImgH,
             "...at the dimensions it was written");
    contract(back.pixels.size() == (size_t)back.width * back.height * 4,
             "...satisfying DecodedImage's own documented size invariant");
    if (back.width != kImgW || back.height != kImgH ||
        back.pixels.size() != wrote.pixels.size()) {
        return;
    }

    // THE ALPHA ASSERTION, which is the one the deleted WIC encoder's warning asked for.
    // A CF_BITMAP has no alpha channel and GetDIBits leaves the fourth byte undefined.
    //
    // WHAT THIS ONE DOES AND DOES NOT PROVE, measured rather than reasoned: deleting the
    // forced-alpha line from the retired src/clipboard_win32.cpp left this check GREEN, because
    // the bytes it is reading back were written by this same seam with alpha 255. So this
    // contract proves the seam does not DAMAGE alpha, and nothing more. The claim that the
    // force is load-bearing needs a producer that publishes zero there, which is what
    // publishZeroAlphaDib supplies further down -- and that one did go red when the line
    // was deleted. Both are kept: this one covers the path the application actually uses.
    unsigned notOpaque = 0;
    for (size_t i = 3; i < back.pixels.size(); i += 4) {
        if (back.pixels[i] != 255) notOpaque++;
    }
    contract(notOpaque == 0,
             "EVERY alpha byte reads 255 -- the forced-opaque contract, which no length check sees");

    // THE COLOUR ASSERTION, per coordinate. This is what makes the round trip a claim
    // about pixels: a flipped row order, a mirrored column order or a swapped B and R
    // channel each move at least one byte off its own coordinate.
    unsigned mismatches = 0;
    for (unsigned y = 0; y < kImgH; y++) {
        for (unsigned x = 0; x < kImgW; x++) {
            const uint8_t* p = back.pixels.data() + ((size_t)y * kImgW + x) * 4;
            if (p[0] != blueAt(x, y) || p[1] != greenAt(x, y) || p[2] != redAt(x, y)) {
                mismatches++;
            }
        }
    }
    std::cout << "  MEASURED colour_mismatches=" << mismatches << " of " << (kImgW * kImgH)
              << " pixels\n";
    contract(mismatches == 0,
             "EVERY colour triple reads back at its OWN coordinate -- rows unflipped, channels unswapped");

    // Row order again, on its own, so a flip has a NAME rather than showing up only as a
    // pixel count. The second half is the part that matters: matching the top row is not
    // enough if the top and bottom rows are interchangeable.
    const uint8_t* first = back.pixels.data();
    contract(first[0] == blueAt(0, 0) && first[0] != blueAt(0, kImgH - 1),
             "the TOP row reads back as the top row, and not as the bottom one");

    // THE pngBytes PARAMETER EXERCISED, not asserted, and the distinction is honest: this
    // seam publishes those bytes under a format it never reads back, so there is no route
    // from here to what a receiving application would get. What CAN be asserted is that
    // publishing them does not disturb the bitmap -- which on Windows means the second
    // SetClipboardData did not displace the first, and on Qt means the image and the
    // image/png entry are on the same QMimeData rather than two that replaced each other.
    const std::string notAPng = "qinta clipboard seam test -- deliberately not a PNG";
    if (platform::setClipboardImage(wrote, notAPng)) {
        const tinta::DecodedImage again = platform::clipboardImage();
        contract(again.ok && again.width == kImgW && again.height == kImgH,
                 "publishing pngBytes alongside does not displace the bitmap");
        // Rewritten WITHOUT the bogus bytes before this function returns, so the test does
        // not leave a format on the user's clipboard whose contents are not a PNG. The
        // text restore in testSeam runs after this and usually supersedes it anyway; this
        // covers the case where there was no text to restore.
        platform::setClipboardImage(wrote, std::string());
    }

#ifdef _WIN32
    // THE FORCED ALPHA, MEASURED AGAINST A PRODUCER THAT DOES NOT COOPERATE. Windows only,
    // because the producer is a raw CF_DIB through the Win32 clipboard. Off Windows there is
    // no equivalent raw publish to write -- QMimeData carries a QImage, not a DIB. Since
    // 2026-10-07 the seam reading it back is clipboard_qt.cpp, so this now measures Qt's
    // windows-plugin DIB read plus its Format_RGB32 conversion, where it used to measure
    // GetDIBits plus the forced-alpha loop in the deleted clipboard_win32.cpp.
    if (publishZeroAlphaDib()) {
        const tinta::DecodedImage zeroed = platform::clipboardImage();
        if (zeroed.ok && zeroed.pixels.size() == (size_t)kImgW * kImgH * 4) {
            unsigned transparent = 0;
            for (size_t i = 3; i < zeroed.pixels.size(); i += 4) {
                if (zeroed.pixels[i] != 255) transparent++;
            }
            std::cout << "  MEASURED transparent_bytes=" << transparent << " of "
                      << (kImgW * kImgH) << " pixels published with alpha 0\n";
            contract(transparent == 0,
                     "a bitmap published with ZERO in the fourth byte reads back OPAQUE -- the "
                     "forced alpha is load-bearing, not decorative");
            // The colours survive that forcing, which is the other half: writing 255 into
            // the alpha byte must not disturb the three beside it, and a premultiplied
            // reading of alpha-0 pixels would have had to zero them.
            unsigned moved = 0;
            for (unsigned y = 0; y < kImgH; y++) {
                for (unsigned x = 0; x < kImgW; x++) {
                    const uint8_t* p = zeroed.pixels.data() + ((size_t)y * kImgW + x) * 4;
                    if (p[0] != blueAt(x, y) || p[1] != greenAt(x, y) || p[2] != redAt(x, y)) {
                        moved++;
                    }
                }
            }
            contract(moved == 0, "...with its colour triples untouched by the forcing");
        } else {
            std::cout << "  SKIP  the zero-alpha producer -- the seam declined to read the"
                         " raw CF_DIB back\n";
        }
        // The valid fixture goes back on the clipboard, so nothing downstream -- or on
        // Alex's desktop -- is left holding a fully transparent bitmap.
        platform::setClipboardImage(wrote, std::string());
    } else {
        std::cout << "  SKIP  the zero-alpha producer -- the raw CF_DIB publish refused\n";
    }
#endif
}

// --- the seam's own two functions ----------------------------------------------
void testSeam() {
    std::cout << "-- the seam, through whichever backend this build selected\n";

    // Asserted in BOTH modes, deliberately: this is a contract, not a computed
    // expectation, and tests/file_path_tests.cpp:207 already relies on it. Emptying
    // the clipboard on an empty request would destroy whatever the user had copied,
    // from a code path that believed it was doing nothing.
    contract(!platform::setClipboardText(L""), "an empty write is refused and touches nothing");

    // WHAT WAS THERE BEFORE IS PUT BACK. See the header note.
    const std::wstring saved = platform::clipboardText();

    const std::wstring sentinel = L"qinta clipboard seam \x263A \x4F60\x597D";
    if (platform::setClipboardText(sentinel)) {
        contract(platform::clipboardText() == sentinel,
              "text written through the seam reads back identically, including non-BMP-adjacent glyphs");

        // C10's MEASUREMENT, not an assertion. Whether the platform hands back the CRLF
        // it was given is a real difference between the two backends and the seam does
        // not promise either answer -- so this is recorded rather than required. An
        // assertion here would encode one platform's behaviour as the contract.
        if (platform::setClipboardText(L"first\r\nsecond")) {
            const std::wstring got = platform::clipboardText();
            const bool crlfSurvived = got == L"first\r\nsecond";
            std::cout << "  MEASURED crlf_round_trip=" << (crlfSurvived ? "preserved" : "normalized")
                      << " read_back_len=" << got.size() << " (wrote 13)\n";
            // The only invariant: whatever it did, the text is still two lines' worth.
            contract(got.find(L"second") != std::wstring::npos,
                  "a CRLF round trip keeps both lines, however it spells the break");
        }
    } else {
        // Not a failure. A build with no QGuiApplication reaches here by design, and
        // saying so beats a green that hid the fact nothing was exercised.
        std::cout << "  SKIP  the round trip -- setClipboardText refused"
                     " (no application instance, or the platform has no clipboard)\n";
    }

    // The image half runs INSIDE the text half's save/restore bracket rather than beside
    // it, and that is deliberate: it clobbers the clipboard the same way, and the restore
    // below is the only thing putting the user's data back.
    testImageSeam();

    if (!saved.empty()) platform::setClipboardText(saved);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--control") control = true;
    }

#ifdef TINTA_CLIPBOARD_BACKEND_QT
    // QClipboard is reached from QGuiApplication and is null without one. Constructed
    // here rather than in testSeam so the transformation checks above run identically
    // on both platforms -- they do not need it, and building one for them would blur
    // the claim that they are independent of the backend.
    QGuiApplication qtApp(argc, argv);
#endif

    std::cout << "=== include/clipboard.h ===\n";
    testNormalizeCrLfOnly();
    testNormalizeNewlines();
    testFlattenToSingleLine();
    testTheThreeDisagree();
    testSeam();

    std::cout << "\nclipboard_failures=" << failures << "\n";
    std::cout << "clipboard_checks=" << checks << "\n";

    if (control) {
        // THE THRESHOLD IS THE COUNTER, NOT A GUESS. Every perturbed check must have
        // failed, so the number that survived must equal the number that was never
        // perturbed. Writing that as "expect roughly this many" is how a control stops
        // being able to tell a loose assertion from a missing one -- and the first
        // version of this file did exactly that, with a hand-written 5 against an
        // actual 7, so it would have passed with two transformation assertions rotten.
        const int survived = checks - failures;
        std::cout << "CONTROL: perturbed_failed=" << failures
                  << " survived=" << survived
                  << " unperturbed_contracts=" << unperturbed << "\n";
        if (survived != unperturbed) {
            std::cout << "CONTROL FAILED: " << (survived - unperturbed)
                      << " perturbed check(s) still passed -- those assertions are not"
                         " anchored to their own result\n";
            return 1;
        }
        std::cout << "CONTROL PASSED: all " << failures
                  << " perturbed transformation assertions missed\n";
        return 0;
    }

    if (failures == 0) std::cout << "All " << checks << " clipboard checks passed.\n";
    return failures == 0 ? 0 : 1;
}

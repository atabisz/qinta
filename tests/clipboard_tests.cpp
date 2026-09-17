// include/clipboard.h -- the clipboard seam's three line-ending transformations, and
// a round trip through whichever backend this build selected.
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

#include <iostream>
#include <string>

#ifdef TINTA_CLIPBOARD_BACKEND_QT
#include <QGuiApplication>
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

// --- input.cpp:351's rule: CR, LF and TAB each -> ONE space --------------------
void testFlattenToSingleLine() {
    std::cout << "-- flattenToSingleLine (input.cpp's and editor.cpp:2252's rule)\n";
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

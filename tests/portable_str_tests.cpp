// Assertions for include/portable_str.h.
//
// This header shipped without a test: tinta::strnicmp had no assertion on either
// platform until this file existed. Both backends run these, which is the point --
// the seam's whole claim is that the two spellings behave alike, and only a suite
// that runs on both can support it.
//
// WHAT IS DELIBERATELY NOT ASSERTED: truncation. A truncating swprintf_s
// fail-fasts on Windows (0xC0000409, measured) and tinta::swprintf reproduces that
// with abort() off Windows, so the case ends the process on both platforms by
// design and cannot live in a suite. It was probed with a standalone executable
// per platform instead. An assertion here would test nothing and kill the run.

#include "portable_str.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

static void checkWide(const wchar_t* got, const wchar_t* want, const char* what) {
    ++checks;
    if (std::wcscmp(got, want) != 0) {
        ++failures;
        // Print as %ls so a mismatch is readable rather than just counted.
        std::printf("FAIL: %s (got \"%ls\" want \"%ls\")\n", what, got, want);
    }
}

// --- strnicmp -----------------------------------------------------------------

static void testStrnicmp() {
    check(tinta::strnicmp("abc", "ABC", 3) == 0, "strnicmp: equal ignoring case");
    check(tinta::strnicmp("abc", "abc", 3) == 0, "strnicmp: equal exactly");
    check(tinta::strnicmp("abc", "ABD", 3) < 0, "strnicmp: less than");
    check(tinta::strnicmp("abd", "ABC", 3) > 0, "strnicmp: greater than");

    // n bounds the comparison: the differing byte sits past it.
    check(tinta::strnicmp("abcX", "ABCY", 3) == 0, "strnicmp: n stops before the difference");
    check(tinta::strnicmp("abcX", "ABCY", 4) != 0, "strnicmp: n includes the difference");

    // n == 0 compares nothing and is equal by definition.
    check(tinta::strnicmp("a", "b", 0) == 0, "strnicmp: n of zero is equal");

    // Digits and punctuation have no case and must compare unchanged.
    check(tinta::strnicmp("a1-", "A1-", 3) == 0, "strnicmp: non-letters unaffected");

    // A shorter string ends at its terminator, which is less than any character.
    check(tinta::strnicmp("ab", "ABC", 3) < 0, "strnicmp: terminator sorts low");
}

// --- swprintf, explicit-count arity -------------------------------------------

static void testSwprintfCount() {
    wchar_t buf[64];

    const int n = tinta::swprintf(buf, 64, L"%d", 42);
    checkWide(buf, L"42", "swprintf(count): integer");
    check(n == 2, "swprintf(count): returns the character count");

    tinta::swprintf(buf, 64, L"%u", 7u);
    checkWide(buf, L"7", "swprintf(count): unsigned");

    tinta::swprintf(buf, 64, L"%ls", L"wide");
    checkWide(buf, L"wide", "swprintf(count): wide string via %ls");

    tinta::swprintf(buf, 64, L"%d × %d %ls", 3, 4, L"table");
    checkWide(buf, L"3 × 4 table", "swprintf(count): the editrail.cpp format");

    // A result that exactly fills the buffer including its terminator is NOT
    // truncation and must return normally. 4 characters plus NUL into 5.
    wchar_t tight[5];
    const int t = tinta::swprintf(tight, 5, L"%ls", L"ABCD");
    checkWide(tight, L"ABCD", "swprintf(count): exact fit is not truncation");
    check(t == 4, "swprintf(count): exact fit returns the count");

    // An empty result is legal and distinct from a failure.
    const int e = tinta::swprintf(buf, 64, L"%ls", L"");
    checkWide(buf, L"", "swprintf(count): empty result");
    check(e == 0, "swprintf(count): empty result returns zero");
}

// --- swprintf, array-deducing arity -------------------------------------------
//
// The overload that takes no count. These call sites are the 10 that had no
// portable spelling at all, so the assertion that matters is that the deduced N
// really is the array's extent and not something else.

static void testSwprintfArray() {
    wchar_t buf[64];

    const int n = tinta::swprintf(buf, L"%d", 42);
    checkWide(buf, L"42", "swprintf(array): integer");
    check(n == 2, "swprintf(array): returns the character count");

    tinta::swprintf(buf, L"%ls-%d", L"x", 9);
    checkWide(buf, L"x-9", "swprintf(array): mixed conversions");

    // Deduction check: a 5-element array must accept a 4-character result. If N
    // were deduced as anything smaller this aborts rather than failing softly,
    // which is itself the signal.
    wchar_t tight[5];
    const int t = tinta::swprintf(tight, L"%ls", L"ABCD");
    checkWide(tight, L"ABCD", "swprintf(array): deduced extent allows an exact fit");
    check(t == 4, "swprintf(array): exact fit returns the count");

    // Both arities must agree on the same inputs -- the seam's core claim.
    wchar_t viaCount[32];
    wchar_t viaArray[32];
    tinta::swprintf(viaCount, 32, L"%d/%ls", 12, L"ab");
    tinta::swprintf(viaArray, L"%d/%ls", 12, L"ab");
    check(std::wcscmp(viaCount, viaArray) == 0, "swprintf: both arities agree");
}

int main() {
    testStrnicmp();
    testSwprintfCount();
    testSwprintfArray();

    if (failures == 0) {
        std::printf("all %d portable-str assertions run, 0 failed\n", checks);
        return 0;
    }
    std::printf("%d of %d portable-str assertion(s) failed\n", failures, checks);
    return 1;
}

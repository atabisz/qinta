// Assertions for src/selection.cpp -- word and line selection semantics, on both
// platforms.
//
// WHY THIS TARGET EXISTS, and what it changes. Until this file, selection.cpp was
// listed in tests/portable-sources.txt under "Compiled off Windows, but NOT linked
// or run", built by the portable_compile_only OBJECT library. That target's own
// comment said, correctly at the time, that "no assertion in this repo executes a
// line of selection.cpp off Windows" and that "this file's own logic is not [tested],
// on either platform, and was not before this commit either." Both halves were
// true. The second half is the more interesting one: the word-selection rules below
// -- including the deliberate trailing-hit rule -- had never been executed by any
// test on any platform.
//
// It became possible to write this because App stopped holding the Direct2D
// wrappers BY VALUE. Linking anything that instantiates an App used to demand
// vtables out of src/render_d2d.cpp and src/text_d2d.cpp, both Windows-only by
// design; now App owns pointers to the abstract seam types, so the vtables come
// from whichever backend was installed, and none needs to be for an App that is
// never drawn. This target is the first thing to actually spend that.
//
// WHAT IS NOT ASSERTED HERE, AND WHERE IT IS. selectionOffsetAtPoint,
// selectionHighlightRects, selectionRangeRect and the populated path through
// selectionLineRange all read app.layoutTextRuns, app.textRects and
// app.lineBuckets, which means a real layout fixture rather than a stack App with
// one string set. That fixture now exists -- tests/app_fixture.h -- and those four
// are asserted by tests/selection_layout_tests.cpp, which found three undocumented
// decisions in them. This file deliberately stays fixture-free: the functions
// below need nothing but app.docText and the selection flags, so a failure here is
// unambiguously about word boundaries rather than about layout arithmetic. The
// empty-container behaviour of selectionLineRange is asserted here for the same
// reason -- it needs no fixture at all.

#include "app.h"
#include "selection.h"

#include <cstdio>
#include <string>

static int failures = 0;
static int checks = 0;

static void checkSize(size_t got, size_t want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %zu, want %zu\n", what, got, want);
    }
}

static void checkBool(bool got, bool want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %s, want %s\n", what,
                    got ? "true" : "false", want ? "true" : "false");
    }
}

static void checkFloat(float got, float want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %f, want %f\n", what, (double)got, (double)want);
    }
}

// Compares a wide selection result against an ASCII expectation. ASCII on purpose:
// the point here is the range arithmetic, and a non-ASCII literal would make the
// assertion depend on this file's encoding surviving two compilers.
static void checkWide(const std::wstring& got, const char* want, const char* what) {
    checks++;
    std::wstring expect;
    for (const char* p = want; *p; ++p) expect.push_back((wchar_t)(unsigned char)*p);
    if (got != expect) {
        failures++;
        std::printf("FAIL %s: got %zu wide chars, want \"%s\" (%zu)\n", what,
                    got.size(), want, expect.size());
    }
}

static std::wstring wide(const char* s) {
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

// A word range, named by what the caller asked for, so a failure reads as a
// sentence rather than as two numbers.
static void expectWord(const char* text, size_t offset, size_t wantStart,
                       size_t wantEnd, const char* what) {
    App app;
    app.docText = wide(text);
    size_t start = 12345, end = 54321;   // poison, so a function that writes
                                         // nothing is caught rather than believed
    selectionWordRange(app, offset, start, end);
    checkSize(start, wantStart, what);
    checkSize(end, wantEnd, what);
}

// --- selectionWordRange ---------------------------------------------------

static void testWordRangeEmptyText() {
    // Empty text is the one case that writes 0,0 and returns early. It gets its
    // own test because the poison values above would otherwise survive and the
    // next assertion would be measuring the poison.
    expectWord("", 0, 0, 0, "empty text yields an empty range");
    expectWord("", 99, 0, 0, "empty text ignores the offset");
}

static void testWordRangeMidWord() {
    //            0123456789
    const char* t = "hello world";
    expectWord(t, 0, 0, 5, "offset at the start of the first word selects it");
    expectWord(t, 2, 0, 5, "offset inside the first word selects the whole word");
    expectWord(t, 4, 0, 5, "offset at the last char of a word selects the word");
    expectWord(t, 7, 6, 11, "offset inside the second word selects it");
    expectWord(t, 10, 6, 11, "offset at the final char selects the last word");
}

// The trailing-hit rule, which selection.cpp comments as "A caret sitting just
// after a word (trailing hit) belongs to that word". It is a real product
// decision -- click past the end of a word and you get the word, not the space --
// and it had no assertion, so a refactor could have quietly dropped it.
static void testWordRangeTrailingHit() {
    const char* t = "hello world";
    expectWord(t, 5, 0, 5, "the space after a word selects the word before it");
    // ...but a boundary char whose predecessor is ALSO a boundary does not walk
    // back, so it selects just itself. This is the other side of the same branch.
    expectWord("hi  there", 3, 3, 4,
               "the second of two spaces selects only itself");
}

static void testWordRangeOnPunctuation() {
    // '.' is a word boundary. At offset 5 the predecessor 'o' is not a boundary,
    // so the trailing-hit rule walks back into "hello" rather than selecting the
    // dot.
    expectWord("hello. world", 5, 0, 5,
               "a period straight after a word selects the word");
    // With a space before it, the punctuation is selected alone.
    expectWord("hello . world", 6, 6, 7,
               "punctuation preceded by a space selects only itself");
}

static void testWordRangeClampsPastEnd() {
    const char* t = "hello";
    // offset >= size clamps to size-1, which lands on 'o' and selects the word.
    expectWord(t, 5, 0, 5, "an offset one past the end clamps into the last word");
    expectWord(t, 999, 0, 5, "a wildly out-of-range offset clamps too");
}

static void testWordRangeSingleChar() {
    expectWord("a", 0, 0, 1, "a one-character word selects itself");
    expectWord(" ", 0, 0, 1, "a lone boundary char selects itself");
}

// --- selectionTextForRange ------------------------------------------------

static void testTextForRange() {
    App app;
    app.docText = wide("hello world");
    checkWide(selectionTextForRange(app, 0, 5), "hello", "a leading range");
    checkWide(selectionTextForRange(app, 6, 11), "world", "a trailing range");
    checkWide(selectionTextForRange(app, 0, 11), "hello world", "the full range");
    checkWide(selectionTextForRange(app, 4, 7), "o w", "a range spanning a space");
}

static void testTextForRangeDegenerate() {
    App app;
    app.docText = wide("hello");
    checkWide(selectionTextForRange(app, 3, 3), "", "an empty range yields nothing");
    checkWide(selectionTextForRange(app, 4, 2), "",
              "an inverted range yields nothing rather than throwing");
    checkWide(selectionTextForRange(app, 99, 200), "",
              "a range entirely past the end yields nothing");
    // Both ends clamp to size(), so this asks for more than exists and gets what
    // exists. The clamp is the reason the substr below cannot throw.
    checkWide(selectionTextForRange(app, 2, 999), "llo",
              "an end past the end clamps to the text length");
}

static void testTextForRangeEmptyDoc() {
    App app;
    checkWide(selectionTextForRange(app, 0, 10), "",
              "an empty document yields nothing for any range");
}

// --- clearSelection ------------------------------------------------------

static void testClearSelection() {
    App app;
    // Dirty every field clearSelection touches, so the assertions below cannot
    // pass by the fields never having been set in the first place -- which is what
    // they would do against a default-constructed App.
    app.selecting = true;
    app.hasSelection = true;
    app.selAnchor = 17;
    app.selFocus = 42;
    app.selAutoScrollVel = 3.5f;
    app.selShiftExtend = true;

    clearSelection(app);

    checkBool(app.selecting, false, "clearSelection clears selecting");
    checkBool(app.hasSelection, false, "clearSelection clears hasSelection");
    checkSize(app.selAnchor, 0, "clearSelection zeroes selAnchor");
    checkSize(app.selFocus, 0, "clearSelection zeroes selFocus");
    checkFloat(app.selAutoScrollVel, 0.0f,
               "clearSelection zeroes the auto-scroll velocity");
    checkBool(app.selShiftExtend, false, "clearSelection clears selShiftExtend");
}

// --- selectionLineRange, empty-container case only ------------------------

static void testLineRangeWithNoBuckets() {
    App app;
    app.docText = wide("hello world");
    size_t start = 12345, end = 54321;
    bool got = selectionLineRange(app, 3, start, end);
    checkBool(got, false, "selectionLineRange reports failure with no line buckets");
    // It must not have written the out params on the failure path. Asserted with
    // the poison values, because "returns false" and "leaves the caller's
    // variables alone" are two different promises and only the first is obvious.
    checkSize(start, 12345, "a failed line range leaves start untouched");
    checkSize(end, 54321, "a failed line range leaves end untouched");
}

int main() {
    testWordRangeEmptyText();
    testWordRangeMidWord();
    testWordRangeTrailingHit();
    testWordRangeOnPunctuation();
    testWordRangeClampsPastEnd();
    testWordRangeSingleChar();
    testTextForRange();
    testTextForRangeDegenerate();
    testTextForRangeEmptyDoc();
    testClearSelection();
    testLineRangeWithNoBuckets();

    if (failures) {
        std::printf("%d of %d selection assertion(s) failed\n", failures, checks);
        return 1;
    }
    std::printf("all %d selection assertions run, 0 failed\n", checks);
    return 0;
}

// src/search.cpp: performSearch, mapSearchMatchesToLayout, scrollToCurrentMatch
// and clearFolderSearch.
//
// WHY THIS FILE EXISTS. search.cpp compiled off Windows but nothing executed a
// line of it on either platform -- it sat in the `portable_compile_only` object
// library, which is the case where the source manifest is the only instrument
// there is. A compile-only file and a passing test look identical from the
// outside: both are green, and only one of them ran.
//
// WHAT MADE IT TESTABLE, and what did not. CMakeLists said search.cpp "would not
// LINK", which was true and overstated. The real debt was ten undefined symbols,
// nine of which live in portable translation units already in the manifest
// (themes.cpp, text_utils.cpp, markdown.cpp, platform_paths_posix.cpp). Exactly
// one -- ensureLayoutComplete -- is stuck in render.cpp, and search.cpp calls it
// at one site, as a stated precondition: docText and textRects must cover the
// whole document before searching. This file supplies that precondition directly
// through tests/app_fixture.h and stubs the call, so BE CLEAR ABOUT THE LIMIT:
// nothing here tests ensureLayoutComplete, and nothing here tests that the real
// relayout path satisfies the precondition. What is tested is every line of
// search.cpp that runs once the precondition holds.
//
// SECOND CONSUMER OF THE FIXTURE, but only of half of it. app_fixture.h said it
// had one caller and invited the next test to reuse rather than rebuild it. This
// is that reuse -- but search.cpp reads docText, textRects and nothing else, so
// addLine() is called with withLayout=false throughout and FakeTextLayout is
// never constructed. Said plainly because "reuses the layout fixture" would
// overstate it: the layout half of that header still has exactly one consumer.
//
// FOUR DECISIONS IN SHIPPED CODE THAT WERE NOT WRITTEN DOWN, found by writing
// the expected values by hand rather than recording what the code returned:
//
//   1. MATCHES DO NOT OVERLAP. performSearch advances by the query length after
//      each hit, so "aa" in "aaaa" is two matches, not three. Whether that is
//      right is a product question; that it is the behaviour is now pinned.
//
//   2. A MATCH SPANNING TWO LINES IS ONE BOUNDING BOX, not two fragments. The
//      union takes min/max on all four edges, so a wrapped match highlights the
//      full rectangle enclosing both lines -- including the gap between them if
//      the lines are not left-aligned with each other. Asserted with the two
//      lines deliberately at different x, so the box is visibly wider than
//      either fragment.
//
//   3. docTextLower IS A CACHE AND IS TRUSTED, never revalidated against
//      docText. Two runs over identical docText and query give different answers
//      depending on what the cache held. Callers own the invalidation; this
//      asserts both directions so a future "optimisation" that skips the reset
//      fails loudly here instead of silently returning no matches.
//
//   4. STALE MAPPING IS CLEARED EVEN WHEN THERE IS NOTHING TO MAP TO.
//      mapSearchMatchesToLayout resets every match BEFORE its early return on an
//      empty textRects, so a match cannot keep a rect index from a previous
//      layout. That ordering is load-bearing and easy to lose in a tidy-up.
//
// kNoTextRect is duplicated below rather than shared: search.cpp's is in an
// anonymous namespace, so the test cannot see it. That duplication is a feature
// here -- if the sentinel ever changed, these assertions would fail.
//
// NOT TESTABLE FROM OUTSIDE, and left honestly uncovered rather than papered
// over: startFolderSearchScan spawns a detached std::thread and walks the
// filesystem, and completeFolderSearch takes a void* that must point at
// FolderScanMsg, a type in search.cpp's anonymous namespace. Neither can be
// reached from another translation unit without changing the file's interface,
// which is not something a test should ask for.

#include "app_fixture.h"
#include "search.h"

#include <cstdio>
#include <cmath>
#include <string>

// The one symbol search.cpp needs from render.cpp. See the note at the top: this
// is a stub, and the fixture below is what actually satisfies the precondition
// the real function exists to establish.
void ensureLayoutComplete(App&) {}

static const size_t kNoTextRect = (size_t)-1;

static int checks = 0;
static int failures = 0;

static void checkSize(size_t got, size_t want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %zu, want %zu\n", what, got, want);
    }
}

static void checkInt(int got, int want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void checkFloat(float got, float want, const char* what) {
    checks++;
    if (std::fabs(got - want) > 0.01f) {
        failures++;
        std::printf("FAIL %s: got %.3f, want %.3f\n", what, got, want);
    }
}

static void checkRect(const tinta::RectF& got, float left, float top, float right,
                      float bottom, const char* what) {
    checks++;
    const bool same = std::fabs(got.left - left) <= 0.01f &&
                      std::fabs(got.top - top) <= 0.01f &&
                      std::fabs(got.right - right) <= 0.01f &&
                      std::fabs(got.bottom - bottom) <= 0.01f;
    if (!same) {
        failures++;
        std::printf("FAIL %s: got (%.2f,%.2f,%.2f,%.2f), want (%.2f,%.2f,%.2f,%.2f)\n",
                    what, got.left, got.top, got.right, got.bottom,
                    left, top, right, bottom);
    }
}

// A search match as performSearch would have produced it, so the mapping tests
// can state their input instead of arriving at it through a search.
static void addMatch(App& app, size_t startPos, size_t length) {
    App::SearchMatch m;
    m.textRectIndex = kNoTextRect;
    m.startPos = startPos;
    m.length = length;
    m.highlightRect = tinta::rectF(0, 0, 0, 0);
    app.searchMatches.push_back(m);
}

// A TextRect whose pixel width is NOT docLength * a round charWidth, which
// addLine cannot produce because it derives right from charWidth. Needed to tell
// the proportional division apart from a coincidence.
static void addRectWithWidth(App& app, const char* text, float left, float top,
                             float right, float bottom) {
    const std::wstring w = fixture::wide(text);
    App::TextRect tr;
    tr.rect = tinta::rectF(left, top, right, bottom);
    tr.docStart = app.docText.size();
    tr.docLength = w.size();
    tr.runIndex = (size_t)-1;
    app.docText += w;
    app.textRects.push_back(tr);
}

// performSearch returns early on a null root, so the tests that are not about that
// guard need one. A childless Document is the smallest thing that satisfies it, and
// it extracts to no text -- which one test below asserts directly.
static qmd::ElementPtr emptyDocument() {
    return std::make_shared<qmd::Element>(qmd::ElementType::Document);
}

// A window-less App with the panel state pinned, because documentViewportWidth
// subtracts panel widths and scrollToCurrentMatch's horizontal arithmetic reads
// it. hwnd stays null, which is also what keeps performSearch out of the folder
// search timer path.
static void resetApp(App& app) {
    app.docText.clear();
    app.docTextLower.clear();
    app.textRects.clear();
    app.lineBuckets.clear();
    app.layoutTextRuns.clear();
    app.searchMatches.clear();
    app.searchQuery.clear();
    app.searchCurrentIndex = 0;
    app.searchMatchCursor = 0;
    app.editMode = false;
    app.showFolderBrowser = false;
    app.showToc = false;
    app.width = 800;
    app.height = 400;
    app.scrollX = 0;
    app.scrollY = 0;
    app.targetScrollX = 0;
    app.targetScrollY = 0;
    app.contentWidth = 2000;
    app.contentHeight = 1000;
    app.root = nullptr;
}

// ---------------------------------------------------------------------------
// mapSearchMatchesToLayout
// ---------------------------------------------------------------------------

static void testMapWithNoMatchesTouchesNothing() {
    App app;
    resetApp(app);
    fixture::addLine(app, "hello", 100, 0, 10, 20, false);
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches.size(), 0, "no matches stays no matches");
}

// Decision 4: the reset runs BEFORE the early return, so a match cannot keep a
// rect index from a layout that no longer exists.
static void testMapClearsStaleMappingWithNoTextRects() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 7;
    app.searchMatches[0].highlightRect = tinta::rectF(1, 2, 3, 4);
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, kNoTextRect,
              "stale rect index cleared with no textRects");
    checkRect(app.searchMatches[0].highlightRect, 0, 0, 0, 0,
              "stale highlight cleared with no textRects");
}

static void testMapInsideOneRect() {
    App app;
    resetApp(app);
    // "hello world": rect (100,0,210,20), 11 chars, so charWidth is exactly 10.
    fixture::addLine(app, "hello world", 100, 0, 10, 20, false);
    addMatch(app, 6, 5);  // "world"
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, 0, "match maps to rect 0");
    // startX = 100 + (6-0)*10 = 160; endX = 160 + 5*10 = 210
    checkRect(app.searchMatches[0].highlightRect, 160, 0, 210, 20,
              "highlight covers exactly the matched characters");
}

// Decision 2. addLine appends with no separator, so docText is contiguous across
// lines and a match can straddle the boundary.
static void testMapSpansTwoRectsAsOneBoundingBox() {
    App app;
    resetApp(app);
    fixture::addLine(app, "hello", 100, 0, 10, 20, false);   // rect0 (100,0,150,20)
    fixture::addLine(app, "world", 300, 20, 10, 20, false);  // rect1 (300,20,350,40)
    addMatch(app, 3, 4);  // "lowo" -- [3,5) in rect0, [5,7) in rect1
    mapSearchMatchesToLayout(app);
    // rect0 fragment (130,0,150,20); rect1 fragment (300,20,320,40).
    // Union: left 130, top 0, right 320, bottom 40 -- wider than either
    // fragment, and it spans the 150..300 gap where nothing is drawn.
    checkSize(app.searchMatches[0].textRectIndex, 0,
              "spanning match keeps the FIRST rect index");
    checkRect(app.searchMatches[0].highlightRect, 130, 0, 320, 40,
              "spanning match is one union bounding box");
}

static void testMapIsIdempotent() {
    App app;
    resetApp(app);
    fixture::addLine(app, "hello", 100, 0, 10, 20, false);
    fixture::addLine(app, "world", 300, 20, 10, 20, false);
    addMatch(app, 3, 4);
    mapSearchMatchesToLayout(app);
    const tinta::RectF first = app.searchMatches[0].highlightRect;
    const size_t firstIndex = app.searchMatches[0].textRectIndex;
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, firstIndex,
              "second mapping keeps the same rect index");
    checkRect(app.searchMatches[0].highlightRect, first.left, first.top,
              first.right, first.bottom, "second mapping keeps the same rect");
}

// A match entirely before the first rect must be skipped without consuming the
// match that does belong to it -- the two-cursor walk (matchIndex, then mi) is
// what makes this work and it is easy to break.
static void testMapSkipsAMatchBeforeTheFirstRect() {
    App app;
    resetApp(app);
    app.docText = fixture::wide("0123456789");
    addRectWithWidth(app, "abcde", 0, 0, 50, 20);  // docStart 10, docLength 5
    addMatch(app, 0, 2);   // entirely before the rect
    addMatch(app, 10, 3);  // "abc"
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, kNoTextRect,
              "match before the first rect stays unmapped");
    checkSize(app.searchMatches[1].textRectIndex, 0,
              "the following match is still mapped");
    checkRect(app.searchMatches[1].highlightRect, 0, 0, 30, 20,
              "the following match gets the right rect");
}

// charWidth is totalWidth / docLength, so a rect 100px wide over 4 characters
// gives 25px per character. Nothing else in these tests would notice if the
// division were replaced by a constant.
static void testMapDividesRectWidthByCharacterCount() {
    App app;
    resetApp(app);
    addRectWithWidth(app, "abcd", 0, 0, 100, 20);
    addMatch(app, 1, 2);  // "bc" -> startX 25, endX 75
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, 0, "wide-glyph match maps");
    checkRect(app.searchMatches[0].highlightRect, 25, 0, 75, 20,
              "character width is the rect width divided by the character count");
}

static void testMapSkipsAZeroLengthRect() {
    App app;
    resetApp(app);
    addRectWithWidth(app, "", 0, 0, 0, 20);          // docLength 0, skipped
    addRectWithWidth(app, "abcde", 100, 20, 150, 40);  // docStart 0 as well
    addMatch(app, 0, 2);
    mapSearchMatchesToLayout(app);
    checkSize(app.searchMatches[0].textRectIndex, 1,
              "an empty rect is skipped rather than dividing by zero");
    checkRect(app.searchMatches[0].highlightRect, 100, 20, 120, 40,
              "the match lands on the first non-empty rect");
}

// ---------------------------------------------------------------------------
// performSearch
// ---------------------------------------------------------------------------

static void testEmptyQueryClearsEverything() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    addMatch(app, 0, 2);
    app.searchCurrentIndex = 3;
    app.searchMatchCursor = 9;
    performSearch(app);
    checkSize(app.searchMatches.size(), 0, "empty query clears matches");
    checkInt(app.searchCurrentIndex, 0, "empty query resets the current index");
    checkSize(app.searchMatchCursor, 0, "empty query resets the match cursor");
}

static void testNullRootFindsNothing() {
    App app;
    resetApp(app);
    app.searchQuery = fixture::wide("hello");
    app.docText = fixture::wide("hello");
    app.docTextLower = fixture::wide("hello");
    performSearch(app);
    checkSize(app.searchMatches.size(), 0, "no document, no matches");
}

// Decision 1.
static void testMatchesDoNotOverlap() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    app.docText = fixture::wide("aaaa");
    app.docTextLower = fixture::wide("aaaa");
    app.searchQuery = fixture::wide("aa");
    performSearch(app);
    checkSize(app.searchMatches.size(), 2, "\"aa\" in \"aaaa\" is two matches, not three");
    checkSize(app.searchMatches[0].startPos, 0, "first match at 0");
    checkSize(app.searchMatches[1].startPos, 2, "second match at 2, not 1");
}

static void testSearchIsCaseInsensitive() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    app.docText = fixture::wide("Hello World");
    app.searchQuery = fixture::wide("WORLD");
    performSearch(app);
    checkSize(app.searchMatches.size(), 1, "uppercase query matches mixed-case text");
    checkSize(app.searchMatches[0].startPos, 6, "match position is in the original text");
    checkSize(app.searchMatches[0].length, 5, "match length is the query length");
}

// Decision 3, asserted in both directions: the same docText and the same query
// give different answers depending on the cache, and clearing it fixes them.
static void testLowercaseCacheIsTrustedNotRevalidated() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    app.docText = fixture::wide("hello");
    app.docTextLower = fixture::wide("xxxxx");  // deliberately inconsistent
    app.searchQuery = fixture::wide("hello");
    performSearch(app);
    checkSize(app.searchMatches.size(), 0,
              "a stale lowercase cache is searched as-is");
    app.docTextLower.clear();
    performSearch(app);
    checkSize(app.searchMatches.size(), 1,
              "clearing the cache is what makes the search correct again");
}

static void testPerformSearchMapsToTheLayout() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    fixture::addLine(app, "hello world", 100, 0, 10, 20, false);
    app.searchQuery = fixture::wide("world");
    performSearch(app);
    checkSize(app.searchMatches.size(), 1, "one match found");
    checkSize(app.searchMatches[0].textRectIndex, 0,
              "performSearch maps its matches before returning");
    checkRect(app.searchMatches[0].highlightRect, 160, 0, 210, 20,
              "and the mapped rect is the one the mapping test derives");
}

// A root with no children extracts no text, so the docText-empty guard returns.
static void testBareRootExtractsNoText() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    app.searchQuery = fixture::wide("hello");
    performSearch(app);
    checkSize(app.docText.size(), 0, "an empty document stays empty");
    checkSize(app.searchMatches.size(), 0, "and yields no matches");
}

static void testQueryLongerThanTheDocument() {
    App app;
    resetApp(app);
    app.root = emptyDocument();
    app.docText = fixture::wide("hi");
    app.searchQuery = fixture::wide("hello");
    performSearch(app);
    checkSize(app.searchMatches.size(), 0, "a query longer than the text finds nothing");
}

// ---------------------------------------------------------------------------
// scrollToCurrentMatch
// ---------------------------------------------------------------------------

static void testScrollWithNoMatchesLeavesScrollAlone() {
    App app;
    resetApp(app);
    app.scrollY = 42;
    app.targetScrollY = 7;
    scrollToCurrentMatch(app);
    checkFloat(app.scrollY, 42, "no matches leaves scrollY alone");
    checkFloat(app.targetScrollY, 7, "no matches leaves targetScrollY alone");
}

static void testScrollWithAnOutOfRangeIndexLeavesScrollAlone() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(0, 500, 10, 520);
    app.scrollY = 42;
    app.searchCurrentIndex = 5;
    scrollToCurrentMatch(app);
    checkFloat(app.scrollY, 42, "an index past the end is ignored");
    app.searchCurrentIndex = -1;
    scrollToCurrentMatch(app);
    checkFloat(app.scrollY, 42, "a negative index is ignored");
}

static void testScrollCentersTheMatchBelowTheSearchBar() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(0, 500, 10, 520);
    scrollToCurrentMatch(app);
    // 500 - (400 - 60)/2 = 500 - 170 = 330; maxScroll = 1000 - 400 = 600
    checkFloat(app.targetScrollY, 330, "target centres the match in the bar-less viewport");
    checkFloat(app.scrollY, 330, "and scrollY follows the target immediately");
}

static void testScrollClampsToTheTop() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(0, 50, 10, 70);
    scrollToCurrentMatch(app);
    // 50 - 170 = -120, clamped to 0
    checkFloat(app.scrollY, 0, "a match near the top clamps to zero rather than going negative");
}

static void testScrollClampsToTheBottom() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(0, 900, 10, 920);
    scrollToCurrentMatch(app);
    // 900 - 170 = 730, clamped to contentHeight - height = 600
    checkFloat(app.scrollY, 600, "a match near the end clamps to the last full page");
}

// An unmapped match has no y, so position is estimated from how far into the
// character stream it sits. This is the path a search hit in a not-yet-laid-out
// part of the document takes.
static void testScrollFallsBackToTheCharacterRatio() {
    App app;
    resetApp(app);
    app.docText.assign(100, L'x');
    addMatch(app, 50, 2);  // textRectIndex stays kNoTextRect
    app.scrollX = 77;
    scrollToCurrentMatch(app);
    // 50/100 * 1000 = 500; 500 - 170 = 330
    checkFloat(app.scrollY, 330, "an unmapped match scrolls by character ratio");
    checkFloat(app.scrollX, 77, "and the horizontal scroll is left untouched");
}

static void testScrollFallbackWithNoTextReturns() {
    App app;
    resetApp(app);
    addMatch(app, 50, 2);
    app.scrollY = 42;
    scrollToCurrentMatch(app);
    checkFloat(app.scrollY, 42, "no docText and no mapping means no scroll at all");
}

static void testScrollKeepsXWhenTheMatchIsAlreadyVisible() {
    App app;
    resetApp(app);
    app.scrollX = 50;  // viewport is [50, 850)
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(100, 0, 200, 20);
    scrollToCurrentMatch(app);
    checkFloat(app.scrollX, 50, "a visible match preserves scrollX rather than resetting it");
}

static void testScrollCentersXWhenTheMatchIsOffToTheRight() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(1000, 0, 1100, 20);
    scrollToCurrentMatch(app);
    // centre 1050 - 800/2 = 650; maxScrollX = 2000 - 800 = 1200
    checkFloat(app.targetScrollX, 650, "an off-screen match is centred horizontally");
    checkFloat(app.scrollX, 650, "and scrollX follows");
}

static void testScrollClampsXToTheContentWidth() {
    App app;
    resetApp(app);
    app.contentWidth = 1000;  // maxScrollX = 1000 - 800 = 200
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(1000, 0, 1100, 20);
    scrollToCurrentMatch(app);
    checkFloat(app.scrollX, 200, "horizontal centring is clamped to the content width");
}

// A hidden panel is the only case documentViewportWidth reduces to app.width, so
// the assertions above depend on it. This pins the dependency: with the TOC up,
// the same match centres against a narrower viewport.
static void testViewportWidthAffectsHorizontalCentring() {
    App app;
    resetApp(app);
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(1000, 0, 1100, 20);
    scrollToCurrentMatch(app);
    const float wide = app.scrollX;
    resetApp(app);
    app.showToc = true;
    addMatch(app, 0, 2);
    app.searchMatches[0].textRectIndex = 0;
    app.searchMatches[0].highlightRect = tinta::rectF(1000, 0, 1100, 20);
    scrollToCurrentMatch(app);
    checks++;
    if (!(app.scrollX > wide)) {
        failures++;
        std::printf("FAIL a narrower viewport scrolls further right: got %.3f, want > %.3f\n",
                    app.scrollX, wide);
    }
}

// ---------------------------------------------------------------------------
// clearFolderSearch
// ---------------------------------------------------------------------------

// The generation counter is how an in-flight scan on another thread is orphaned:
// completeFolderSearch drops any message whose generation no longer matches.
static void testClearFolderSearchOrphansTheScanInFlight() {
    App app;
    resetApp(app);
    app.folderSearchGeneration = 5;
    app.folderResults.emplace_back();
    app.folderResultHits.emplace_back();
    clearFolderSearch(app);
    checkInt(app.folderSearchGeneration, 6, "clearing bumps the generation");
    checkSize(app.folderResults.size(), 0, "clearing drops the results");
    checkSize(app.folderResultHits.size(), 0, "clearing drops the paint-time hits");
}

int main() {
    testMapWithNoMatchesTouchesNothing();
    testMapClearsStaleMappingWithNoTextRects();
    testMapInsideOneRect();
    testMapSpansTwoRectsAsOneBoundingBox();
    testMapIsIdempotent();
    testMapSkipsAMatchBeforeTheFirstRect();
    testMapDividesRectWidthByCharacterCount();
    testMapSkipsAZeroLengthRect();

    testEmptyQueryClearsEverything();
    testNullRootFindsNothing();
    testMatchesDoNotOverlap();
    testSearchIsCaseInsensitive();
    testLowercaseCacheIsTrustedNotRevalidated();
    testPerformSearchMapsToTheLayout();
    testBareRootExtractsNoText();
    testQueryLongerThanTheDocument();

    testScrollWithNoMatchesLeavesScrollAlone();
    testScrollWithAnOutOfRangeIndexLeavesScrollAlone();
    testScrollCentersTheMatchBelowTheSearchBar();
    testScrollClampsToTheTop();
    testScrollClampsToTheBottom();
    testScrollFallsBackToTheCharacterRatio();
    testScrollFallbackWithNoTextReturns();
    testScrollKeepsXWhenTheMatchIsAlreadyVisible();
    testScrollCentersXWhenTheMatchIsOffToTheRight();
    testScrollClampsXToTheContentWidth();
    testViewportWidthAffectsHorizontalCentring();

    testClearFolderSearchOrphansTheScanInFlight();

    // The count is printed on success as well as failure, deliberately: a test
    // binary that prints nothing when it passes is indistinguishable from one
    // whose assertions never ran.
    if (failures == 0) {
        std::printf("search: all %d assertions passed\n", checks);
        return 0;
    }
    std::printf("search: %d of %d assertions FAILED\n", failures, checks);
    return 1;
}

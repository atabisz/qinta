// The half of src/selection.cpp that needs a LAYOUT: selectionOffsetAtPoint,
// selectionHighlightRects, selectionRangeRect, and the populated path through
// selectionLineRange.
//
// selection_tests.cpp asserts the functions that need only docText, and says at
// its top that these four were not covered because no layout fixture existed.
// tests/app_fixture.h is that fixture, so this file closes the gap it named.
//
// WHY THESE WERE UNCOVERED FOR SO LONG, which is also why the assertions below
// are written the way they are: producing this state for real needs a render
// target, a DirectWrite factory and a window, so the only thing that had ever
// executed this arithmetic was a human dragging a mouse over a document and not
// noticing anything wrong. That is a test of the composition, not of the parts.
// Every expected number here is instead derived by hand from the code under
// test, against a layout whose answers are exact by construction (10px per
// character, 20px per line). A test that recorded whatever the code returned
// would be a change detector; these are claims about what is correct.
//
// THREE THINGS THIS FOUND that are worth reading before changing selection.cpp,
// because each is a decision rather than an accident and none was written down:
//
//   1. Bucket ranges OVERLAP at a boundary. selectionLineRange matches on
//      `lo <= offset && offset <= hi`, both ends inclusive, and returns the
//      FIRST bucket that matches -- so the offset at a line break belongs to the
//      line that ends there, not the one that starts there.
//
//   2. The two range-rect functions disagree about a degenerate metric.
//      selectionHighlightRects requires `metrics.width > 0` and falls back to
//      caret positions when a backend answers with zero width;
//      selectionRangeRect checks only `count == 1` and will happily return an
//      empty rect. Asserted in both directions below rather than tidied, because
//      which one is right depends on the caller and neither caller is here.
//
//   3. The caret-pair fallback AGREES with the primary path. Same input, same
//      rect, whether the backend answers the range query, refuses it, or answers
//      with zero width. That agreement is the entire justification for the
//      fallback existing, and nothing had ever checked it -- a fallback that
//      quietly returned a different rect would have looked like a rendering
//      glitch under an unrelated feature.
//
// NOT ASSERTED, deliberately. The `right > left` guard in the caret-pair
// fallback is unreachable through a monospace layout, because the caller
// guarantees lo < hi and monospace makes distinct offsets distinct positions. A
// fake could be bent to reach it, but the bend would be the test's own
// invention rather than anything a backend does, so the branch is left honestly
// uncovered instead of covered by a fiction.

#include "app_fixture.h"
#include "selection.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int checks = 0;
static int failures = 0;

static void checkBool(bool got, bool want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %s, want %s\n", what,
                    got ? "true" : "false", want ? "true" : "false");
    }
}

static void checkSize(size_t got, size_t want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %zu, want %zu\n", what, got, want);
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

static const float CW = 10.0f;  // one character
static const float LH = 20.0f;  // one line

// Two lines, NO covering layout: every text rect keeps runIndex at its default,
// so runFor() returns nullptr and the interpolation paths are the ones that run.
// This is the real shape of a syntax-highlighted code block.
static void buildGeometryOnly(App& app) {
    fixture::addLine(app, "hello", 100.0f, 0.0f, CW, LH, false);
    fixture::addLine(app, "world!", 100.0f, LH, CW, LH, false);
    // docText == "helloworld!" -- 11 characters, line 0 owns [0,5), line 1 [5,11).
}

// The same two lines WITH covering layouts, and the second line starting at a
// different x so that a rect built from the layout's origin cannot accidentally
// match one built from the text rect's left edge.
static void buildWithLayouts(App& app) {
    fixture::addLine(app, "hello", 100.0f, 0.0f, CW, LH, true);
    fixture::addLine(app, "world!", 200.0f, LH, CW, LH, true);
}

// ---------------------------------------------------------------- offset at point

static void testOffsetAtPointEmptyApp() {
    App app;
    checkSize(selectionOffsetAtPoint(app, 120.0f, 10.0f), 0,
              "offsetAtPoint: no buckets at all");
}

static void testOffsetAtPointEmptyDocText() {
    // Buckets present but docText empty. A separate guard from the one above,
    // and reachable in practice for a moment during a reload.
    App app;
    buildGeometryOnly(app);
    app.docText.clear();
    checkSize(selectionOffsetAtPoint(app, 120.0f, 10.0f), 0,
              "offsetAtPoint: buckets but empty docText");
}

static void testOffsetAtPointInterpolates() {
    App app;
    buildGeometryOnly(app);

    // Line 0 spans x 100..150 for 5 characters, so 10px per character.
    checkSize(selectionOffsetAtPoint(app, 100.0f, 10.0f), 0,
              "offsetAtPoint: left edge of line 0");
    checkSize(selectionOffsetAtPoint(app, 150.0f, 10.0f), 5,
              "offsetAtPoint: right edge of line 0");
    checkSize(selectionOffsetAtPoint(app, 130.0f, 10.0f), 3,
              "offsetAtPoint: 60% across line 0");
    // Exactly halfway across a 5-character line is 2.5 characters, and the code
    // rounds with lround -- half away from zero, so 3 rather than 2. Asserted
    // because it is the kind of thing a well-meaning switch to a cast would
    // silently change.
    checkSize(selectionOffsetAtPoint(app, 125.0f, 10.0f), 3,
              "offsetAtPoint: halfway rounds away from zero");
}

static void testOffsetAtPointPicksNearestLineVertically() {
    App app;
    buildGeometryOnly(app);

    // Far below both lines: line 1 (bottom 40) is nearer than line 0 (bottom 20).
    checkSize(selectionOffsetAtPoint(app, 100.0f, 100.0f), 5,
              "offsetAtPoint: below everything snaps to the last line");
    // Far above both: line 0 is nearer.
    checkSize(selectionOffsetAtPoint(app, 150.0f, -50.0f), 5,
              "offsetAtPoint: above everything snaps to the first line");
    // Exactly on the shared edge, y == 20, is zero distance from BOTH buckets.
    // The scan keeps the first on a tie (`dist < bestDist`), so line 0 wins and
    // x=100 gives offset 0 rather than line 1's offset 5.
    checkSize(selectionOffsetAtPoint(app, 100.0f, LH), 0,
              "offsetAtPoint: a vertical tie keeps the earlier line");
}

static void testOffsetAtPointSnapsFromTheMargin() {
    App app;
    buildGeometryOnly(app);

    // Left of the text: select from the line's start.
    checkSize(selectionOffsetAtPoint(app, 50.0f, 10.0f), 0,
              "offsetAtPoint: left margin of line 0");
    checkSize(selectionOffsetAtPoint(app, 50.0f, 30.0f), 5,
              "offsetAtPoint: left margin of line 1");
    // Right of the text: select through the line's end.
    checkSize(selectionOffsetAtPoint(app, 500.0f, 10.0f), 5,
              "offsetAtPoint: right margin of line 0");
    checkSize(selectionOffsetAtPoint(app, 500.0f, 30.0f), 11,
              "offsetAtPoint: right margin of line 1");
}

static void testOffsetAtPointClampsToDocText() {
    // Both returns are wrapped in std::min(..., docText.size()), which only
    // matters when the layout outlives the text it described -- exactly the
    // window between a document being replaced and the relayout finishing.
    // Shrinking docText under a stale layout reproduces it.
    App app;
    buildGeometryOnly(app);
    app.docText.resize(8);

    checkSize(selectionOffsetAtPoint(app, 500.0f, 30.0f), 8,
              "offsetAtPoint: margin snap clamped to a shrunken docText");
    checkSize(selectionOffsetAtPoint(app, 160.0f, 30.0f), 8,
              "offsetAtPoint: in-rect hit clamped to a shrunken docText");
}

static void testOffsetAtPointSnapsToGlyphBoundaries() {
    // The layout path, which exists because interpolation put the caret in the
    // middle of a glyph (#83). Line 0 is at x 100 with 10px characters, so the
    // fake reports a trailing hit past the midpoint of a character and the caret
    // moves to the far side of it.
    App app;
    buildWithLayouts(app);

    checkSize(selectionOffsetAtPoint(app, 100.0f, 10.0f), 0,
              "glyph snap: start of the first character");
    checkSize(selectionOffsetAtPoint(app, 104.0f, 10.0f), 0,
              "glyph snap: before the midpoint stays put");
    checkSize(selectionOffsetAtPoint(app, 106.0f, 10.0f), 1,
              "glyph snap: past the midpoint moves on");
    checkSize(selectionOffsetAtPoint(app, 145.0f, 10.0f), 4,
              "glyph snap: exactly on a midpoint does not advance");
    checkSize(selectionOffsetAtPoint(app, 148.0f, 10.0f), 5,
              "glyph snap: past the last midpoint reaches the end");
    // The right edge counts as inside the rect, and the clamp to the run's own
    // range keeps it at the end rather than one past it.
    checkSize(selectionOffsetAtPoint(app, 150.0f, 10.0f), 5,
              "glyph snap: right edge clamps to the run end");
    // Second line, whose run origin is 200 rather than 100 -- proving the
    // point is translated into the run's space and not the rect's.
    checkSize(selectionOffsetAtPoint(app, 200.0f, 30.0f), 5,
              "glyph snap: start of a run with a shifted origin");
    checkSize(selectionOffsetAtPoint(app, 226.0f, 30.0f), 8,
              "glyph snap: mid-run with a shifted origin");
}

// ------------------------------------------------------------------- line range

static void testLineRangePopulated() {
    App app;
    buildGeometryOnly(app);
    size_t start = 12345, end = 54321;

    checkBool(selectionLineRange(app, 0, start, end), true, "lineRange: offset 0 found");
    checkSize(start, 0, "lineRange: offset 0 start");
    checkSize(end, 5, "lineRange: offset 0 end");

    checkBool(selectionLineRange(app, 3, start, end), true, "lineRange: offset 3 found");
    checkSize(start, 0, "lineRange: offset 3 start");
    checkSize(end, 5, "lineRange: offset 3 end");

    checkBool(selectionLineRange(app, 6, start, end), true, "lineRange: offset 6 found");
    checkSize(start, 5, "lineRange: offset 6 start");
    checkSize(end, 11, "lineRange: offset 6 end");

    checkBool(selectionLineRange(app, 11, start, end), true,
              "lineRange: last offset found");
    checkSize(start, 5, "lineRange: last offset start");
    checkSize(end, 11, "lineRange: last offset end");
}

static void testLineRangeBoundaryBelongsToTheEarlierLine() {
    // Offset 5 is the end of line 0 AND the start of line 1, because the match
    // is inclusive at both ends. The first bucket wins. Finding 1 in the header.
    App app;
    buildGeometryOnly(app);
    size_t start = 12345, end = 54321;

    checkBool(selectionLineRange(app, 5, start, end), true,
              "lineRange: boundary offset found");
    checkSize(start, 0, "lineRange: boundary offset takes the earlier line's start");
    checkSize(end, 5, "lineRange: boundary offset takes the earlier line's end");
}

static void testLineRangePastTheEnd() {
    App app;
    buildGeometryOnly(app);
    size_t start = 12345, end = 54321;

    checkBool(selectionLineRange(app, 12, start, end), false,
              "lineRange: past the last line is not found");
    checkSize(start, 12345, "lineRange: miss leaves start untouched");
    checkSize(end, 54321, "lineRange: miss leaves end untouched");
}

// ------------------------------------------------------------- highlight rects

static void testHighlightEmptyRange() {
    App app;
    buildGeometryOnly(app);
    std::vector<tinta::RectF> out;

    selectionHighlightRects(app, 3, 3, out);
    checkSize(out.size(), 0, "highlight: empty range produces nothing");
    selectionHighlightRects(app, 5, 2, out);
    checkSize(out.size(), 0, "highlight: inverted range produces nothing");
}

static void testHighlightAppendsRatherThanReplaces() {
    // The function takes the vector by reference and pushes onto it. A caller
    // that forgets to clear gets last frame's rects too, so the behaviour is
    // pinned rather than left to be discovered.
    App app;
    buildGeometryOnly(app);
    std::vector<tinta::RectF> out;
    out.push_back(tinta::rectF(1.0f, 2.0f, 3.0f, 4.0f));

    selectionHighlightRects(app, 0, 5, out);
    checkSize(out.size(), 2, "highlight: appends to a non-empty vector");
    checkRect(out[0], 1.0f, 2.0f, 3.0f, 4.0f, "highlight: pre-existing rect survives");
}

static void testHighlightGeometryFallback() {
    App app;
    buildGeometryOnly(app);

    std::vector<tinta::RectF> out;
    selectionHighlightRects(app, 0, 5, out);
    checkSize(out.size(), 1, "highlight/geometry: one line selected gives one rect");
    checkRect(out[0], 100.0f, 0.0f, 150.0f, LH, "highlight/geometry: whole first line");

    // Across the line break: 3 characters of line 0 and 3 of line 1.
    out.clear();
    selectionHighlightRects(app, 2, 8, out);
    checkSize(out.size(), 2, "highlight/geometry: two lines give two rects");
    checkRect(out[0], 120.0f, 0.0f, 150.0f, LH,
              "highlight/geometry: tail of the first line");
    checkRect(out[1], 100.0f, LH, 130.0f, 2 * LH,
              "highlight/geometry: head of the second line");
}

static void testHighlightUsesTheLayoutWhenThereIsOne() {
    App app;
    buildWithLayouts(app);

    std::vector<tinta::RectF> out;
    selectionHighlightRects(app, 1, 3, out);
    checkSize(out.size(), 1, "highlight/layout: one rect, not one per path");
    checkRect(out[0], 110.0f, 0.0f, 130.0f, LH, "highlight/layout: characters 1..3");

    // Spanning both runs, the second of which starts at x 200.
    out.clear();
    selectionHighlightRects(app, 3, 8, out);
    checkSize(out.size(), 2, "highlight/layout: one rect per covered run");
    checkRect(out[0], 130.0f, 0.0f, 150.0f, LH, "highlight/layout: tail of run 0");
    checkRect(out[1], 200.0f, LH, 230.0f, 2 * LH, "highlight/layout: head of run 1");
}

static void testHighlightDoesNotPaintACoveredRectTwice() {
    // Every text rect here points at a run with a layout, so the geometry loop
    // at the bottom of the function must skip all of them. If it did not, each
    // selected line would yield two overlapping rects and selection would render
    // double-darkened. The count IS the assertion.
    App app;
    buildWithLayouts(app);

    std::vector<tinta::RectF> out;
    selectionHighlightRects(app, 0, 11, out);
    checkSize(out.size(), 2, "highlight: covered rects are not also interpolated");
}

static void testHighlightSkipsRunsWithNoTextRect() {
    // A run nobody points at is not selectable text -- table padding is the case
    // that motivated the check. Built by hand because the fixture always pairs a
    // run with a rect, which is what a well-formed layout looks like.
    App app;
    buildWithLayouts(app);

    App::LayoutTextRun orphan;
    orphan.layout = std::make_shared<fixture::FakeTextLayout>(5, CW, LH);
    orphan.pos = tinta::pointF(400.0f, 0.0f);
    orphan.bounds = tinta::rectF(400.0f, 0.0f, 450.0f, LH);
    orphan.docStart = 0;
    orphan.docLength = 5;
    orphan.selectable = true;
    app.layoutTextRuns.push_back(std::move(orphan));

    std::vector<tinta::RectF> out;
    selectionHighlightRects(app, 0, 5, out);
    checkSize(out.size(), 1, "highlight: an unreferenced run is not highlighted");
    checkRect(out[0], 100.0f, 0.0f, 150.0f, LH,
              "highlight: the referenced run is still highlighted");
}

static void testHighlightCaretFallbackAgreesWithTheRangeQuery() {
    // Finding 3 in the header. Same selection, three backend behaviours, and the
    // rect must not move.
    const float wantLeft = 130.0f, wantRight = 150.0f;

    {
        App app;
        buildWithLayouts(app);
        std::vector<tinta::RectF> out;
        selectionHighlightRects(app, 3, 5, out);
        checkSize(out.size(), 1, "highlight/agree: range query gives one rect");
        checkRect(out[0], wantLeft, 0.0f, wantRight, LH,
                  "highlight/agree: range query rect");
    }
    {
        App app;
        buildWithLayouts(app);
        fixture::layoutFor(app, 0)->rangeMode =
            fixture::FakeTextLayout::RangeMode::ReturnsFalse;
        std::vector<tinta::RectF> out;
        selectionHighlightRects(app, 3, 5, out);
        checkSize(out.size(), 1, "highlight/agree: refused query still gives one rect");
        checkRect(out[0], wantLeft, 0.0f, wantRight, LH,
                  "highlight/agree: caret pair matches the range query");
    }
    {
        App app;
        buildWithLayouts(app);
        fixture::layoutFor(app, 0)->rangeMode =
            fixture::FakeTextLayout::RangeMode::ZeroWidth;
        std::vector<tinta::RectF> out;
        selectionHighlightRects(app, 3, 5, out);
        checkSize(out.size(), 1, "highlight/agree: zero-width answer is rejected");
        checkRect(out[0], wantLeft, 0.0f, wantRight, LH,
                  "highlight/agree: zero width falls back to the caret pair");
    }
}

// ------------------------------------------------------------------ range rect

static void testRangeRectOutsideTheRect() {
    App app;
    buildGeometryOnly(app);
    tinta::RectF out = tinta::rectF(7.0f, 7.0f, 7.0f, 7.0f);

    checkBool(selectionRangeRect(app, app.textRects[0], 6, 9, out), false,
              "rangeRect: a selection past this rect is false");
    checkRect(out, 7.0f, 7.0f, 7.0f, 7.0f, "rangeRect: a false return writes nothing");

    checkBool(selectionRangeRect(app, app.textRects[0], 4, 4, out), false,
              "rangeRect: an empty selection is false");
}

static void testRangeRectInterpolates() {
    App app;
    buildGeometryOnly(app);
    tinta::RectF out{};

    checkBool(selectionRangeRect(app, app.textRects[0], 1, 3, out), true,
              "rangeRect/geometry: found");
    checkRect(out, 110.0f, 0.0f, 130.0f, LH, "rangeRect/geometry: characters 1..3");

    // A selection wider than the rect is clipped to it, not extrapolated.
    checkBool(selectionRangeRect(app, app.textRects[0], 0, 100, out), true,
              "rangeRect/geometry: oversized selection found");
    checkRect(out, 100.0f, 0.0f, 150.0f, LH,
              "rangeRect/geometry: oversized selection is clipped to the rect");
}

static void testRangeRectUsesTheLayout() {
    App app;
    buildWithLayouts(app);
    tinta::RectF out{};

    checkBool(selectionRangeRect(app, app.textRects[1], 6, 9, out), true,
              "rangeRect/layout: found");
    // Run 1 sits at x 200 and owns offsets 5..11, so 6..9 is 10..40 into it.
    checkRect(out, 210.0f, LH, 240.0f, 2 * LH, "rangeRect/layout: characters 6..9");
}

static void testRangeRectFallsBackWhenTheQueryIsRefused() {
    App app;
    buildWithLayouts(app);
    fixture::layoutFor(app, 0)->rangeMode =
        fixture::FakeTextLayout::RangeMode::ReturnsFalse;
    tinta::RectF out{};

    checkBool(selectionRangeRect(app, app.textRects[0], 1, 3, out), true,
              "rangeRect/refused: still found");
    checkRect(out, 110.0f, 0.0f, 130.0f, LH,
              "rangeRect/refused: interpolation matches the layout answer");
}

static void testRangeRectAcceptsAZeroWidthMetric() {
    // Finding 2 in the header, and the one place these two functions differ:
    // selectionHighlightRects rejects a zero-width metric and re-derives the
    // span, while this one checks only the metric count and returns the empty
    // rect. Whether that is a defect depends on what the caller does with an
    // empty rect, so the current behaviour is recorded rather than changed --
    // and recorded loudly, so a future change is a decision and not a surprise.
    App app;
    buildWithLayouts(app);
    fixture::layoutFor(app, 0)->rangeMode =
        fixture::FakeTextLayout::RangeMode::ZeroWidth;
    tinta::RectF out{};

    checkBool(selectionRangeRect(app, app.textRects[0], 1, 3, out), true,
              "rangeRect/zero-width: returns true");
    checkFloat(out.left, 110.0f, "rangeRect/zero-width: left is the metric's left");
    checkFloat(out.right, 110.0f, "rangeRect/zero-width: right collapses onto left");
}

int main() {
    testOffsetAtPointEmptyApp();
    testOffsetAtPointEmptyDocText();
    testOffsetAtPointInterpolates();
    testOffsetAtPointPicksNearestLineVertically();
    testOffsetAtPointSnapsFromTheMargin();
    testOffsetAtPointClampsToDocText();
    testOffsetAtPointSnapsToGlyphBoundaries();

    testLineRangePopulated();
    testLineRangeBoundaryBelongsToTheEarlierLine();
    testLineRangePastTheEnd();

    testHighlightEmptyRange();
    testHighlightAppendsRatherThanReplaces();
    testHighlightGeometryFallback();
    testHighlightUsesTheLayoutWhenThereIsOne();
    testHighlightDoesNotPaintACoveredRectTwice();
    testHighlightSkipsRunsWithNoTextRect();
    testHighlightCaretFallbackAgreesWithTheRangeQuery();

    testRangeRectOutsideTheRect();
    testRangeRectInterpolates();
    testRangeRectUsesTheLayout();
    testRangeRectFallsBackWhenTheQueryIsRefused();
    testRangeRectAcceptsAZeroWidthMetric();

    if (failures == 0) {
        std::printf("selection layout: all %d assertions passed\n", checks);
        return 0;
    }
    std::printf("selection layout: %d of %d assertions FAILED\n", failures, checks);
    return 1;
}

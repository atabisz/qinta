#ifndef TINTA_TESTS_APP_FIXTURE_H
#define TINTA_TESTS_APP_FIXTURE_H

// A synthetic App layout, for asserting code that takes an App& without a window,
// a device or a real text backend.
//
// WHY THIS EXISTS. App's layout state -- docText, textRects, lineBuckets,
// layoutTextRuns -- is produced by the relayout path, which needs a render target,
// a DirectWrite factory and a window. So every function reading it was untestable
// in practice even on Windows, and untestable in principle off it until App stopped
// naming a backend in its storage. selection_tests covers what needs only docText;
// this header covers what needs a LAYOUT, which is the larger and more interesting
// half.
//
// ONE CONSUMER TODAY: tests/selection_layout_tests.cpp. Said plainly because a
// fixture header with one caller looks like speculative generality and this one is
// a deliberate bet -- the remaining App&-taking files (overlays.cpp,
// math_render.cpp, annotations.cpp) all read the same four members, so the next
// test to want this should reuse it rather than rebuild it. If that stops being
// true, delete the header rather than keeping it out of politeness.
//
// WHAT THE FAKE LAYOUT IS AND IS NOT. FakeTextLayout implements tinta::TextLayout
// as an exact monospace grid: every character is charWidth wide, one line tall.
// That is NOT a claim about how DirectWrite or Qt behave -- those are asserted
// against each other by render_api_tests and qt_text_tests, which is the right
// place for it. The point here is the opposite: a layout whose answers are known
// exactly, so the arithmetic in selection.cpp can be checked against numbers
// derived by hand rather than against whatever the platform happened to return.
//
// It also reaches branches a real backend cannot easily be made to reach. The
// HitTestTextRange path in selectionHighlightRects has a documented fallback for
// when the backend returns no single range metric; rangeMode below turns that
// fallback on deliberately. Nothing had ever executed it.

#include "app.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace fixture {

// An exact monospace layout over [0, textLength) characters.
class FakeTextLayout final : public tinta::TextLayout {
public:
    // How HitTestTextRange should behave. The default is the normal case; the
    // other two exist to reach selectionHighlightRects' fallback branch, which is
    // real code with no coverage and no easy way to provoke it from a real
    // backend.
    enum class RangeMode {
        OneMetric,     // the normal case: one metric, positive width
        ReturnsFalse,  // backend refuses the query
        ZeroWidth      // backend answers, but with a degenerate width
    };

    FakeTextLayout(size_t textLength, float charWidth, float height)
        : len_(textLength), cw_(charWidth), h_(height) {}

    RangeMode rangeMode = RangeMode::OneMetric;

    // --- the part that carries meaning ------------------------------------

    bool HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                      bool* isInside, tinta::HitTestMetrics* metrics) const override {
        if (len_ == 0) return false;
        float x = pointX < 0.0f ? 0.0f : pointX;
        size_t pos = (size_t)(x / cw_);
        if (pos >= len_) pos = len_ - 1;
        // Trailing once past the midpoint of the cluster, which is the rule
        // selection.cpp relies on to snap the caret to a glyph boundary.
        float within = x - (float)pos * cw_;
        if (isTrailingHit) *isTrailingHit = within > cw_ * 0.5f;
        if (isInside) {
            *isInside = pointX >= 0.0f && pointX < (float)len_ * cw_ &&
                        pointY >= 0.0f && pointY < h_;
        }
        if (metrics) {
            *metrics = tinta::HitTestMetrics{};
            metrics->textPosition = (uint32_t)pos;
            metrics->length = 1;
            metrics->left = (float)pos * cw_;
            metrics->top = 0.0f;
            metrics->width = cw_;
            metrics->height = h_;
            metrics->isText = 1;
        }
        return true;
    }

    bool HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                             float* pointX, float* pointY,
                             tinta::HitTestMetrics* metrics) const override {
        size_t pos = textPosition;
        if (pos > len_) pos = len_;
        float x = (float)pos * cw_ + (isTrailingHit ? cw_ : 0.0f);
        if (pointX) *pointX = x;
        if (pointY) *pointY = 0.0f;
        if (metrics) {
            *metrics = tinta::HitTestMetrics{};
            metrics->textPosition = (uint32_t)pos;
            metrics->length = pos < len_ ? 1 : 0;
            metrics->left = (float)pos * cw_;
            metrics->width = pos < len_ ? cw_ : 0.0f;
            metrics->height = h_;
            metrics->isText = 1;
        }
        return true;
    }

    bool HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                          float originX, float originY,
                          tinta::HitTestMetrics* metrics,
                          uint32_t maxMetricsCount,
                          uint32_t* actualMetricsCount) const override {
        if (rangeMode == RangeMode::ReturnsFalse) {
            if (actualMetricsCount) *actualMetricsCount = 0;
            return false;
        }
        if (maxMetricsCount < 1 || !metrics) {
            if (actualMetricsCount) *actualMetricsCount = 1;
            return false;
        }
        *metrics = tinta::HitTestMetrics{};
        metrics->textPosition = textPosition;
        metrics->length = textLength;
        metrics->left = originX + (float)textPosition * cw_;
        metrics->top = originY;
        metrics->width = rangeMode == RangeMode::ZeroWidth
                             ? 0.0f
                             : (float)textLength * cw_;
        metrics->height = h_;
        metrics->isText = 1;
        if (actualMetricsCount) *actualMetricsCount = 1;
        return true;
    }

    void GetMetrics(tinta::TextMetrics* metrics) const override {
        if (!metrics) return;
        *metrics = tinta::TextMetrics{};
        metrics->width = (float)len_ * cw_;
        metrics->widthIncludingTrailingWhitespace = (float)len_ * cw_;
        metrics->height = h_;
        metrics->layoutWidth = (float)len_ * cw_;
        metrics->layoutHeight = h_;
        metrics->lineCount = 1;
    }

    // --- the part that only has to exist ----------------------------------
    //
    // Deliberately inert. Nothing under test calls these, and giving them
    // behaviour would be inventing a contract rather than asserting one. They are
    // listed rather than stubbed via a macro so that a new pure virtual on
    // tinta::TextLayout breaks this file loudly, which is the point of writing the
    // fake against the real interface.

    bool GetLineMetrics(tinta::LineMetrics* lineMetrics, uint32_t maxLineCount,
                        uint32_t* actualLineCount) const override {
        if (actualLineCount) *actualLineCount = 1;
        if (!lineMetrics || maxLineCount < 1) return false;
        *lineMetrics = tinta::LineMetrics{};
        return true;
    }

    bool GetClusterMetrics(tinta::ClusterMetrics* clusterMetrics,
                          uint32_t maxClusterCount,
                          uint32_t* actualClusterCount) const override {
        if (actualClusterCount) *actualClusterCount = (uint32_t)len_;
        if (!clusterMetrics || maxClusterCount < len_) return false;
        for (size_t i = 0; i < len_; i++) {
            clusterMetrics[i] = tinta::ClusterMetrics{};
        }
        return true;
    }

    void SetFontWeight(tinta::FontWeight, tinta::TextRange) override {}
    void SetFontSize(float, tinta::TextRange) override {}
    void SetStrikethrough(bool, tinta::TextRange) override {}

    void SetTextAlignment(tinta::TextAlignment) override {}
    void SetParagraphAlignment(tinta::ParagraphAlignment) override {}
    void SetWordWrapping(tinta::WordWrapping) override {}
    void SetTrimming(const tinta::Trimming&, tinta::InlineObject*) override {}
    void SetLineSpacing(tinta::LineSpacingMethod, float, float) override {}
    float GetFontSize() const override { return h_; }

    size_t length() const { return len_; }
    float charWidth() const { return cw_; }

private:
    size_t len_;
    float cw_;
    float h_;
};

inline std::wstring wide(const char* s) {
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

// Appends one line of text to app: a TextRect, a LineBucket containing it, and
// optionally a LayoutTextRun with a FakeTextLayout covering exactly that range.
//
// Coordinates are chosen by the caller rather than computed here, because the
// assertions are about which rect a point lands in and that has to be legible in
// the test rather than hidden in a helper.
//
// Returns the index of the TextRect it added.
inline size_t addLine(App& app, const char* text, float left, float top,
                      float charWidth, float lineHeight, bool withLayout) {
    const size_t docStart = app.docText.size();
    const std::wstring w = wide(text);
    app.docText += w;

    const float right = left + (float)w.size() * charWidth;
    const float bottom = top + lineHeight;

    App::TextRect tr;
    tr.rect = tinta::rectF(left, top, right, bottom);
    tr.docStart = docStart;
    tr.docLength = w.size();
    tr.runIndex = (size_t)-1;

    if (withLayout) {
        App::LayoutTextRun run;
        run.layout = std::make_shared<FakeTextLayout>(w.size(), charWidth,
                                                      lineHeight);
        run.pos = tinta::pointF(left, top);
        run.bounds = tinta::rectF(left, top, right, bottom);
        run.docStart = docStart;
        run.docLength = w.size();
        run.selectable = true;
        app.layoutTextRuns.push_back(std::move(run));
        tr.runIndex = app.layoutTextRuns.size() - 1;
    }

    app.textRects.push_back(tr);
    const size_t rectIndex = app.textRects.size() - 1;

    App::LineBucket b;
    b.top = top;
    b.bottom = bottom;
    b.minX = left;
    b.maxX = right;
    b.textRectIndices.push_back(rectIndex);
    app.lineBuckets.push_back(std::move(b));

    return rectIndex;
}

// The FakeTextLayout behind a text rect, so a test can change its RangeMode.
// Returns nullptr when the rect has no covering run, which is itself a state the
// tests assert.
inline FakeTextLayout* layoutFor(App& app, size_t rectIndex) {
    if (rectIndex >= app.textRects.size()) return nullptr;
    const size_t runIndex = app.textRects[rectIndex].runIndex;
    if (runIndex >= app.layoutTextRuns.size()) return nullptr;
    return static_cast<FakeTextLayout*>(app.layoutTextRuns[runIndex].layout.get());
}

}  // namespace fixture

#endif  // TINTA_TESTS_APP_FIXTURE_H

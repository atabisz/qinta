#include "render.h"
#include "i18n.h"
#include "inline_style.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "mermaid.h"
#include "mermaid_ext.h"
#include "math_render.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <thread>

// urlmon.h, d2d1_3.h and shlwapi.h used to sit here, unguarded, and they were what
// made this file FATAL off Windows -- so nothing past them was ever measured, and
// six spans read `errors=1, fatal=1` at urlmon.h:19 as if it were a fact about the
// file. All three are now behind include/image_api.h; the census that found them
// confined to two functions is in that header. The two `#pragma comment(lib, ...)`
// lines went with them and were redundant besides: CMakeLists.txt:236,242 already
// links urlmon and shlwapi.
#include "image_api.h"

// Fit-to-width override lookup for a table/diagram ordinal key
static bool fitBlockEnabled(const App& app, unsigned key) {
    return std::find(app.fitBlocks.begin(), app.fitBlocks.end(), key) !=
           app.fitBlocks.end();
}

namespace {
constexpr float kHugeWidth = 100000.0f;
constexpr float kLineBucketTolerance = 5.0f;
// Horizontal padding drawn around an inline `code` pill
constexpr float kCodeSpanPadding = 4.0f;

// OWNING, since 2026-09-15, and that is the substance of the change rather than a
// side effect of it. The old member was a raw IDWriteTextLayout* whose ownership
// moved by convention: four call sites called Release() by hand (two of them inside
// a measure-and-discard loop), one handed the pointer to a D2DTextLayout, and one
// nulled the field to signal a transfer. All six of those are gone -- the shared_ptr
// says what each of them said in comments, and the compiler enforces it.
struct LayoutInfo {
    std::shared_ptr<tinta::TextLayout> layout;
    float width = 0.0f;
    float height = 0.0f;
};

// THE DECISION IS STILL HERE. THE DIRECTWRITE IS NOT.
//
// This used to open "THE ONE NATIVE REACH-THROUGH LEFT IN THIS FILE'S TEXT PATH" and
// hold an IDWriteTextLayout2 QI, an IDWriteFontFallback and a Release. Those five lines
// were the reason this file could not compile off Windows at line 81, and they now live
// in D2DTextLayout::ApplyFontFallback, moved verbatim.
//
// THE REFUSAL THAT KEPT THEM HERE IS NOT REVERSED, and the difference is worth being
// exact about, because "it moved into the backend" is precisely what that refusal said
// not to do. What it said not to do was fold the chain into
// D2DTextFactory::CreateTextLayout, so that every layout the factory makes gets one:
// 15 sites then created through app.textFactory -- utils.cpp's measureText, tabs.cpp,
// overlays.cpp, the retired main_d2d.cpp, export_html.cpp, editrail.cpp -- and NOT ONE had ever had
// a fallback (`git log -S SetFontFallback -- src/utils.cpp` returns nothing). Several of
// them measure text, so handing them a chain silently moves measured widths and
// therefore layout.
//
// That is still true and still refused. The factory now HOLDS the chain and does not
// apply it; a layout gets one only where a caller asks, and this function is the one
// caller in the tree. The 15 sites call CreateTextLayout and nothing else, so their
// widths cannot move -- which is a stronger guarantee than the old arrangement gave,
// because it is now enforced by which method a caller calls rather than by which file
// the QI happens to sit in.
//
// `app` is no longer read. The null-chain guard that used to be `!app.fontFallback` is
// inside the backend now, where the chain is, and reads it at the same moment this did.
// The parameter stays because all three call sites pass it and because dropping it is a
// signature change with no portability content.
static void applyEmojiFallback(App& app, tinta::TextLayout* layout) {
    (void)app;
    if (!layout) return;
    layout->ApplyFontFallback();
}

// ONE overload now, where there were two. The native-format version existed only to
// feed app.dwriteFactory->CreateTextLayout; measured before the change, all 12 call
// sites already passed a tinta::TextFormat*, so the second overload was an adapter
// with no callers of its own and it disappears rather than moving.
//
// The typography argument is a const& to a value where it used to be a nullable COM
// pointer, so the `if (typography)` guard is gone: an empty Typography applies
// nothing, which text_api.h states as the contract and typography_tests.cpp asserts.
// That is a guard removed because the type made it unreachable, not one dropped.
static LayoutInfo createLayout(App& app, std::wstring_view text,
                               tinta::TextFormat* format, float lineHeight,
                               const tinta::Typography& typography) {
    LayoutInfo info;
    if (!format || text.empty() || !app.textFactory) return info;

    info.layout = app.textFactory->CreateTextLayout(
        text.data(), static_cast<uint32_t>(text.length()), format,
        kHugeWidth, lineHeight);
    if (!info.layout) return info;

    info.layout->SetTypography(typography,
                               {0, static_cast<uint32_t>(text.length())});
    applyEmojiFallback(app, info.layout.get());

    tinta::TextMetrics metrics{};
    info.layout->GetMetrics(&metrics);
    info.width = metrics.widthIncludingTrailingWhitespace;
    info.height = metrics.height;
    return info;
}

static void addTextRect(App& app, const tinta::RectF& rect, size_t docStart, size_t docLength,
                        size_t runIndex = (size_t)-1) {
    size_t idx = app.textRects.size();
    app.textRects.push_back({rect, docStart, docLength, runIndex});

    // Table cells are laid out column by column. A wrapped cell can append a
    // later line before the next column appends its first line, so buckets
    // cannot be grouped by looking only at the last one.
    for (auto& bucket : app.lineBuckets) {
        if (std::abs(rect.top - bucket.top) <= kLineBucketTolerance) {
            bucket.bottom = std::max(bucket.bottom, rect.bottom);
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
            bucket.textRectIndices.push_back(idx);
            return;
        }
    }

    if (app.lineBuckets.empty() || rect.top > app.lineBuckets.back().top) {
        App::LineBucket bucket;
        bucket.top = rect.top;
        bucket.bottom = rect.bottom;
        bucket.minX = rect.left;
        bucket.maxX = rect.right;
        bucket.textRectIndices.push_back(idx);
        app.lineBuckets.push_back(std::move(bucket));
        return;
    }

    App::LineBucket bucket;
    bucket.top = rect.top;
    bucket.bottom = rect.bottom;
    bucket.minX = rect.left;
    bucket.maxX = rect.right;
    bucket.textRectIndices.push_back(idx);
    auto insertAt = std::lower_bound(
        app.lineBuckets.begin(), app.lineBuckets.end(), rect.top,
        [](const App::LineBucket& existing, float top) {
            return existing.top < top;
        });
    app.lineBuckets.insert(insertAt, std::move(bucket));
}

static void addTextRun(App& app, LayoutInfo&& info, const tinta::PointF& pos,
                       const tinta::RectF& bounds, tinta::Color color,
                       size_t docStart, size_t docLength, bool selectable) {
    if (!info.layout) return;

    App::LayoutTextRun run;
    // A MOVE, and the prediction that got it here is worth keeping. Last span this
    // line read `std::make_shared<D2DTextLayout>(info.layout, app.dwriteFactory)`,
    // wrapping a native layout because createLayout still made one, and the comment
    // said: "when createLayout eventually moves onto the seam it returns a unique_ptr
    // that converts to this shared_ptr for free, so the member does not change
    // again." That is exactly what happened -- App::LayoutTextRun::layout is
    // untouched by this commit, and this site stops naming a backend at all.
    //
    // It also means this file no longer constructs a D2DTextLayout anywhere, taking
    // the population from three to two (text_d2d.cpp's factory and math_render.cpp's
    // maths-box retain). text_d2d.h's constructor comment records both the count and
    // the fact that it has now been wrong once.
    run.layout = std::move(info.layout);
    run.pos = pos;
    run.bounds = bounds;
    run.color = color;
    run.docStart = docStart;
    run.docLength = docLength;
    run.selectable = selectable;
    app.layoutTextRuns.push_back(run);

    if (selectable) {
        addTextRect(app, bounds, docStart, docLength, app.layoutTextRuns.size() - 1);
    }
}

struct LayoutSnapshot {
    size_t textRuns, rects, lines, shapes, connectors, bitmaps;
    size_t links, textRects, lineBuckets, docTextLen, tasks;
};

static LayoutSnapshot takeSnapshot(App& app) {
    return {
        app.layoutTextRuns.size(),
        app.layoutRects.size(),
        app.layoutLines.size(),
        app.layoutShapes.size(),
        app.layoutConnectors.size(),
        app.layoutBitmaps.size(),
        app.linkRects.size(),
        app.textRects.size(),
        app.lineBuckets.size(),
        app.docText.size(),
        app.taskRects.size()
    };
}

static void rollbackTo(App& app, const LayoutSnapshot& s) {
    // BOTH hand-written release loops are gone now. run.layout is a shared_ptr and
    // shape.geometry is a unique_ptr, so every resize() below IS the release for
    // whatever it truncates away.
    //
    // This function is the reason the path seam's ownership question had one answer:
    // a partial layout is unwound by shrinking these vectors, so the path a shape
    // holds must die with the shape. Anything shared would have leaked here or been
    // freed twice, silently, on a code path that only runs when a layout chunk
    // overruns its time budget.
    app.layoutTextRuns.resize(s.textRuns);
    app.layoutRects.resize(s.rects);
    app.layoutLines.resize(s.lines);
    app.layoutShapes.resize(s.shapes);
    app.layoutConnectors.resize(s.connectors);
    app.layoutBitmaps.resize(s.bitmaps);
    app.linkRects.resize(s.links);
    app.textRects.resize(s.textRects);
    app.lineBuckets.resize(s.lineBuckets);
    app.docText.resize(s.docTextLen);
    app.taskRects.resize(s.tasks);
}

// [top, bottom) limits the shift to the items anchored on one laid-out line, which
// is how aligned paragraphs move each line by its own amount
static void shiftLayoutItems(App& app, const LayoutSnapshot& from, float dx,
                             float top = std::numeric_limits<float>::lowest(),
                             float bottom = std::numeric_limits<float>::max()) {
    if (dx == 0.0f) return;
    auto inRow = [&](float y) { return y >= top && y < bottom; };
    for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); i++) {
        auto& r = app.layoutTextRuns[i];
        if (!inRow(r.bounds.top)) continue;
        r.pos.x += dx;
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.rects; i < app.layoutRects.size(); i++) {
        auto& r = app.layoutRects[i];
        if (!inRow(r.rect.top)) continue;
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.lines; i < app.layoutLines.size(); i++) {
        auto& r = app.layoutLines[i];
        if (!inRow(std::min(r.p1.y, r.p2.y))) continue;
        r.p1.x += dx;
        r.p2.x += dx;
    }
    for (size_t i = from.shapes; i < app.layoutShapes.size(); i++) {
        auto& shape = app.layoutShapes[i];
        if (!inRow(shape.rect.top)) continue;
        shape.rect.left += dx;
        shape.rect.right += dx;
    }
    for (size_t i = from.connectors; i < app.layoutConnectors.size(); i++) {
        auto& connector = app.layoutConnectors[i];
        if (!inRow(connector.bounds.top)) continue;
        connector.bounds.left += dx;
        connector.bounds.right += dx;
        for (auto& point : connector.points) point.x += dx;
    }
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); i++) {
        auto& bitmap = app.layoutBitmaps[i].destRect;
        if (!inRow(bitmap.top)) continue;
        bitmap.left += dx;
        bitmap.right += dx;
    }
    for (size_t i = from.links; i < app.linkRects.size(); i++) {
        auto& r = app.linkRects[i];
        if (!inRow(r.bounds.top)) continue;
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.textRects; i < app.textRects.size(); i++) {
        auto& r = app.textRects[i];
        if (!inRow(r.rect.top)) continue;
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.tasks; i < app.taskRects.size(); i++) {
        if (!inRow(app.taskRects[i].rect.top)) continue;
        app.taskRects[i].rect.left += dx;
        app.taskRects[i].rect.right += dx;
    }

    // A cell can share a line bucket with content laid out before it. Rebuild
    // the affected bucket extents from their rectangles after the shift.
    for (auto& bucket : app.lineBuckets) {
        bool affected = false;
        for (size_t idx : bucket.textRectIndices) {
            if (idx >= from.textRects) {
                affected = true;
                break;
            }
        }
        if (!affected) continue;

        bucket.minX = std::numeric_limits<float>::max();
        bucket.maxX = std::numeric_limits<float>::lowest();
        for (size_t idx : bucket.textRectIndices) {
            const auto& rect = app.textRects[idx].rect;
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
        }
    }
}

// Center or right-align every line laid out since `from` within [startX, startX +
// maxWidth] (align uses Element::align's codes). Lines are found from the word rects,
// and each moves by its own slack; a line wider than the box stays where it is.
static void alignLaidOutLines(App& app, const LayoutSnapshot& from, float startX,
                              float maxWidth, int align) {
    if (align != 2 && align != 3) return;
    struct Row { float top, bottom, minX, maxX; };
    std::vector<Row> rows;
    for (size_t i = from.textRects; i < app.textRects.size(); i++) {
        const auto& r = app.textRects[i].rect;
        auto row = std::find_if(rows.begin(), rows.end(),
                                [&](const Row& w) { return std::abs(w.top - r.top) < 0.5f; });
        if (row == rows.end()) {
            rows.push_back({r.top, r.bottom, r.left, r.right});
        } else {
            row->bottom = std::max(row->bottom, r.bottom);
            row->minX = std::min(row->minX, r.left);
            row->maxX = std::max(row->maxX, r.right);
        }
    }
    auto offset = [&](float left, float right) {
        float slack = maxWidth - (right - left);
        if (slack <= 0) return 0.0f;
        return (align == 2 ? startX + slack / 2 : startX + slack) - left;
    };
    for (const auto& row : rows)
        shiftLayoutItems(app, from, offset(row.minX, row.maxX), row.top - 0.5f, row.bottom);

    // Images, and the placeholder an unloaded image draws, break out of the text flow and
    // own no word rects, so they are not on any row above: each aligns by its own width
    auto onRow = [&](float top) {
        return std::any_of(rows.begin(), rows.end(), [&](const Row& w) {
            return top >= w.top - 0.5f && top < w.bottom;
        });
    };
    // Images sharing a row (their vertical extents overlap) move together
    std::vector<size_t> pending;
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); i++)
        if (!onRow(app.layoutBitmaps[i].destRect.top)) pending.push_back(i);
    while (!pending.empty()) {
        tinta::RectF extent = app.layoutBitmaps[pending.front()].destRect;
        std::vector<size_t> row, rest;
        for (size_t i : pending) {
            const auto& r = app.layoutBitmaps[i].destRect;
            if (r.top < extent.bottom && r.bottom > extent.top) {
                row.push_back(i);
                extent.left = std::min(extent.left, r.left);
                extent.right = std::max(extent.right, r.right);
            } else {
                rest.push_back(i);
            }
        }
        float dx = offset(extent.left, extent.right);
        for (size_t i : row) {
            auto& rect = app.layoutBitmaps[i].destRect;
            for (size_t j = from.links; j < app.linkRects.size(); j++) {
                auto& link = app.linkRects[j].bounds;
                if (link.top == rect.top && link.left == rect.left) { link.left += dx; link.right += dx; }
            }
            rect.left += dx;
            rect.right += dx;
        }
        pending.swap(rest);
    }
    for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); i++) {
        auto& run = app.layoutTextRuns[i];
        if (onRow(run.bounds.top)) continue;
        float dx = offset(run.bounds.left, run.bounds.right);
        run.pos.x += dx;
        run.bounds.left += dx;
        run.bounds.right += dx;
    }
}

// Distance from the top of a laid-out line of `format` to its baseline
static float textBaseline(App& app, tinta::TextFormat* format) {
    float baseline = format->GetFontSize() * 0.8f;
    if (auto probe = app.textFactory->CreateTextLayout(L"Ag", 2, format, 1000, 1000)) {
        tinta::LineMetrics metrics{};
        uint32_t count = 1;
        if (probe->GetLineMetrics(&metrics, 1, &count)) baseline = metrics.baseline;
    }
    return baseline;
}

// Height of one laid-out line of `format` -- the glyph box, not the 1.7x line slot
static float textGlyphHeight(App& app, tinta::TextFormat* format) {
    float height = format->GetFontSize() * 1.2f;
    if (auto probe = app.textFactory->CreateTextLayout(L"Ag", 2, format, 1000, 1000)) {
        tinta::LineMetrics metrics{};
        uint32_t count = 1;
        if (probe->GetLineMetrics(&metrics, 1, &count)) height = metrics.height;
    }
    return height;
}

// Where a ~~strike~~ crosses a run of `format`, below the top of its drawn text: a third
// of the ascent above the baseline, which is where Qt draws a font's own strike-out
// (strike_layout_tests compares the two). It was a fixed fraction of the 1.7x line slot,
// which put the line on the baseline, where it read as an underline.
static float strikeDrop(App& app, tinta::TextFormat* format) {
    return textBaseline(app, format) * (2.0f / 3.0f);
}

static float getSpaceWidth(App& app, tinta::TextFormat* format) {
    if (format == app.textFormat) return app.spaceWidthText;
    if (format == app.boldFormat) return app.spaceWidthBold;
    if (format == app.italicFormat) return app.spaceWidthItalic;
    if (format == app.codeFormat) return app.spaceWidthCode;
    return measureText(app, L" ", format);
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
static App::ImageEntry& getOrLoadImage(App& app, const std::string& src);
static bool imageDisplaySize(App& app, const ElementPtr& elem, float maxWidth,
                             float& displayW, float& displayH);

// --- UAX#14 line-break analysis ---
//
// The 75-line IDWriteTextAnalysisSource/Sink pair that used to sit here has moved to
// src/text_d2d.cpp behind tinta::TextFactory::AnalyzeLineBreaks, with its CJK reasoning
// intact. What stays here is the part that is not DirectWrite's: the ASCII fast path,
// and the space-only fallback for when a backend cannot answer.
//
// The seam call replaces a direct app.textAnalyzer use, and App::textAnalyzer is gone
// with it -- the analyzer is now created lazily by the backend on first non-ASCII
// paragraph rather than eagerly at startup for every session.

namespace {

// canBreak[i] == true when a line may break before text[i]; canBreak[len]
// is always true. Falls back to space-only breaking if analysis fails.
void analyzeBreakOpportunities(App& app, const std::wstring& text, std::vector<bool>& canBreak) {
    canBreak.assign(text.size() + 1, false);
    if (text.empty()) return;
    canBreak[text.size()] = true;

    // Fast path: plain ASCII prose with ordinary token lengths breaks at
    // spaces exactly like the full algorithm — skip the analyzer round trip
    bool simple = true;
    size_t tokenLen = 0;
    for (wchar_t c : text) {
        if (c >= 0x80) { simple = false; break; }
        if (c == L' ') tokenLen = 0;
        else if (++tokenLen > 60) { simple = false; break; }  // long URLs etc. want real breaks
    }
    if (simple) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
        return;
    }

    // A false return means the backend did not analyse -- not that there are no
    // breaks -- so the fallback below is reached on exactly the conditions that
    // reached it before: no analyzer, or an analyzer that failed.
    bool analyzed = false;
    if (app.textFactory) {
        analyzed = app.textFactory->AnalyzeLineBreaks(
            text.c_str(), static_cast<uint32_t>(text.size()), canBreak);
    }
    if (!analyzed) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
    }
}

} // namespace

static void layoutInlineContent(App& app, const std::vector<ElementPtr>& elements,
                                float startX, float& y, float maxWidth,
                                tinta::TextFormat* baseFormat, tinta::Color baseColor,
                                const std::string& baseLinkUrl = {}, float customLineHeight = 0.0f) {
    float x = startX;
    float lineHeight = customLineHeight > 0 ? customLineHeight : baseFormat->GetFontSize() * 1.7f;
    const float normalLineHeight = lineHeight;
    float mathTextOffset = 0;
    float maxX = startX + maxWidth;
    float spaceWidth = getSpaceWidth(app, baseFormat);

    // Links are marked by colour alone. textTop is where the run's text was drawn, so
    // the .md reference dash sits under the glyphs' baseline rather than at the foot
    // of the 1.7x line box, which left a visible gap under every link.
    auto addLinkSegment = [&](float lineStartX, float lineEndX, float lineY,
                              const std::string& linkUrl, tinta::Color color,
                              tinta::TextFormat* format, float textTop) {
        if (lineEndX <= lineStartX) return;
        bool refLive = linkUrl.rfind("fileref-ok:", 0) == 0;
        bool refMissing = linkUrl.rfind("fileref-missing:", 0) == 0;
        if (refLive || refMissing) {
            tinta::TextFormat* f = format ? format : baseFormat;
            float underlineY = textTop + textBaseline(app, f) +
                               std::max(1.0f, f->GetFontSize() * 0.12f);
            // .md references underline dashed; a missing target keeps a
            // faint ghost of the same treatment, and its click offers to
            // create the file (#127)
            tinta::Color dashColor = color;
            dashColor.a *= 0.55f;
            float dash = normalLineHeight * 0.16f;
            float gap = normalLineHeight * 0.12f;
            for (float sx = lineStartX; sx < lineEndX; sx += dash + gap) {
                float ex = std::min(sx + dash, lineEndX);
                app.layoutLines.push_back({tinta::pointF(sx, underlineY),
                                           tinta::pointF(ex, underlineY),
                                           dashColor, 1.0f});
            }
        }
        App::LinkRect lr;
        lr.bounds = tinta::rectF(lineStartX, lineY, lineEndX, lineY + lineHeight);
        lr.url = linkUrl;
        app.linkRects.push_back(lr);
    };

    InlineStyle rootStyle;
    rootStyle.format = baseFormat;
    rootStyle.color = baseColor;
    rootStyle.linkUrl = baseLinkUrl;
    rootStyle.isLink = !baseLinkUrl.empty();
    std::vector<StyledRun> runs;
    flattenInline(app, elements, rootStyle, lineHeight, runs);
    // Where a block image or a hard break left y. A paragraph that ends on either adds
    // no empty line after it: HTML's rule, under which "a<br>" is one line and a lone
    // <br> is one empty line, where both used to take one line more.
    float yAfterImage = -1.0f;
    float yAfterBreak = -1.0f;
    // The row of images being filled side by side: its first bitmap, the link rects
    // added since it started, and its extent
    size_t imageRowFirst = (size_t)-1;
    size_t imageRowLinks = 0;
    float imageRowTop = 0, imageRowBottom = 0, imageRowRight = 0;

    // Reserve a common baseline and enough vertical room before retaining
    // any runs. Uniform spacing within this paragraph keeps wrapping and
    // selection consistent even when a tall inline matrix occurs mid-line.
    std::vector<MathBoxPtr> mathBoxes;
    std::vector<float> mathBaselines;
    float mathBelow = 0;
    for (size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (run.elem->type != ElementType::MathInline && run.elem->type != ElementType::MathDisplay) continue;
        if (mathBoxes.empty()) {
            mathBoxes.resize(runs.size());
            mathBaselines.resize(runs.size(), 0);
        }
        tinta::TextFormat* format = run.style.format ? run.style.format : baseFormat;
        float size = format->GetFontSize();
        mathBoxes[i] = mathParse(app, toWide(run.elem->text), size, false);
        if (!mathBoxes[i]) continue;
        float baseline = textBaseline(app, format);
        mathBaselines[i] = baseline;
        mathExpandLine(mathBoxes[i], baseline + run.style.drawYOffset, normalLineHeight, mathTextOffset, mathBelow);
    }
    lineHeight += mathTextOffset + mathBelow;

    for (size_t runIndex = 0; runIndex < runs.size(); runIndex++) {
        const auto& run = runs[runIndex];
        const ElementPtr& elem = run.elem;
        tinta::TextFormat* format = run.style.format;
        tinta::Color color = run.style.color;
        const std::string& linkUrl = run.style.linkUrl;
        bool isLink = run.style.isLink;
        bool hasBg = run.style.hasBg;
        bool hasStrike = run.style.hasStrike;
        tinta::Color bgColor = run.style.bgColor;
        float drawYOffset = run.style.drawYOffset + mathTextOffset;
        // ==highlight== hugs the glyphs where they are drawn, not the whole line slot,
        // which hung half a font-size of marker below the descenders
        float bgHeight = hasBg ? textGlyphHeight(app, format ? format : baseFormat) : 0.0f;
        // Below the line's y, which moves as the run wraps
        float strikeOffset = hasStrike ? drawYOffset + strikeDrop(app, format ? format : baseFormat) : 0.0f;

        std::wstring text;

        switch (elem->type) {
            case ElementType::Text:
                text = toWide(elem->text);
                break;

            case ElementType::Code: {
                tinta::TextFormat* surroundFormat = format ? format : baseFormat;
                format = inlineCodeFormat(app, run.style.bold, run.style.italic);
                if (!isLink) color = themeInlineCodeColor(app.theme);
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }

                size_t codeDocStart = app.docText.size();
                LayoutInfo info = createLayout(app, text, format, lineHeight, app.codeTypography);
                float textWidth = info.width;

                if (x + textWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                // The code sits on the surrounding text's baseline, measured through
                // the backend. It was centred in the 1.7x line box from an assumed
                // 1.2x font height, which matched Segoe UI and Consolas but put Menlo
                // 3px below Helvetica Neue on macOS. The chip wraps the code's own
                // line rather than the whole line box.
                float verticalOffset = drawYOffset + textBaseline(app, surroundFormat) -
                                       textBaseline(app, format);
                float chipPad = format->GetFontSize() * 0.12f;
                float lineTop = y + mathTextOffset;
                app.layoutRects.push_back({tinta::rectF(x - 2,
                        std::max(lineTop, y + verticalOffset - chipPad),
                        x + textWidth + kCodeSpanPadding,
                        std::min(lineTop + normalLineHeight,
                                 y + verticalOffset + info.height + chipPad)),
                    app.theme.codeBackground});

                // Struck across the code's own glyphs, as GitHub strikes <del><code>
                if (hasStrike) {
                    float strikeY = y + verticalOffset + strikeDrop(app, format);
                    app.layoutLines.push_back({tinta::pointF(x, strikeY),
                                               tinta::pointF(x + textWidth, strikeY), color, 1.0f});
                }
                tinta::PointF pos = tinta::pointF(x, y + verticalOffset);
                tinta::RectF bounds = tinta::rectF(x, y, x + textWidth, y + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, color,
                           codeDocStart, text.length(), true);

                app.docText += text;
                if (isLink) addLinkSegment(x, x + textWidth, y, linkUrl, color, format, y + verticalOffset);
                // Advance by the pill's own padding only: the surrounding text
                // already carries the source's spacing, so a full space width
                // here would double it and shove a following comma off the span
                x += textWidth + kCodeSpanPadding;
                continue;
            }

            case ElementType::MathInline:
            case ElementType::MathDisplay: {
                // Native inline math (#80): an atomic measured box in the
                // text flow, baseline-aligned with the surrounding line
                std::wstring latex = toWide(elem->text);
                MathBoxPtr box = mathBoxes[runIndex];
                if (!box) {
                    // Unsupported TeX: show the raw source in code style
                    format = inlineCodeFormat(app, run.style.bold, run.style.italic);
                    if (!isLink) color = themeInlineCodeColor(app.theme);
                    text = L"$" + latex + L"$";
                    break;
                }
                float w = mathBoxWidth(box);
                if (x + w > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }
                // Align the box baseline with the surrounding text baseline
                float textBaseline = mathBaselines[runIndex];
                float boxTop = y + drawYOffset + textBaseline - mathBoxBaseline(box);
                if (hasBg) {
                    app.layoutRects.push_back({tinta::rectF(x - 2,
                        std::min(boxTop, y + drawYOffset), x + w + 2,
                        std::max(boxTop + mathBoxHeight(box), y + drawYOffset + bgHeight)),
                        bgColor});
                }
                if (hasStrike) {
                    float strikeY = y + strikeOffset;
                    app.layoutLines.push_back({tinta::pointF(x, strikeY),
                                               tinta::pointF(x + w, strikeY), color, 1.0f});
                }
                mathBoxRetain(app, box, x, boxTop, color);
                if (isLink) addLinkSegment(x, x + w, y, linkUrl, color, format, y + drawYOffset);

                size_t docStart = app.docText.size();
                std::wstring source = L"$" + latex + L"$";
                app.docText += source;
                addTextRect(app,
                            tinta::rectF(x, y, x + w, y + lineHeight),
                            docStart, source.size());
                x += w;
                continue;
            }

            case ElementType::SoftBreak:
                text = L" ";
                break;

            case ElementType::HardBreak:
                app.docText += L"\n";
                x = startX;
                y += lineHeight;
                yAfterBreak = y;
                continue;

            case ElementType::Image: {
                // Images break out of the text flow onto rows of their own, but
                // consecutive ones share a row, a space apart and bottom-aligned, as
                // a README's badges do -- wrapping when the next one does not fit
                size_t bitmapsBefore = app.layoutBitmaps.size();
                float w = 0, h = 0;
                bool besideRow = imageRowFirst != (size_t)-1 && x == startX && y == yAfterImage &&
                                 imageDisplaySize(app, elem, maxWidth, w, h) &&
                                 imageRowRight + spaceWidth + w <= startX + maxWidth;
                if (besideRow) {
                    float rowH = imageRowBottom - imageRowTop;
                    if (h > rowH) {
                        // A taller image lowers the row's shared bottom edge
                        float dy = h - rowH;
                        for (size_t i = imageRowFirst; i < app.layoutBitmaps.size(); i++) {
                            auto& r = app.layoutBitmaps[i].destRect;
                            for (size_t j = imageRowLinks; j < app.linkRects.size(); j++) {
                                auto& link = app.linkRects[j].bounds;
                                if (link.top == r.top && link.left == r.left) { link.top += dy; link.bottom += dy; }
                            }
                            r.top += dy;
                            r.bottom += dy;
                        }
                        imageRowBottom = imageRowTop + h;
                    }
                    float left = imageRowRight + spaceWidth;
                    app.layoutBitmaps.push_back({getOrLoadImage(app, elem->url).bitmap,
                        tinta::rectF(left, imageRowBottom - h, left + w, imageRowBottom)});
                    imageRowRight = left + w;
                    y = imageRowBottom + 12 * (app.contentScale * app.zoomFactor);
                } else {
                    if (x > startX) {
                        y += lineHeight;  // end current line
                        x = startX;
                    }
                    layoutImage(app, elem, y, startX, maxWidth);
                    imageRowFirst = (size_t)-1;
                    if (app.layoutBitmaps.size() > bitmapsBefore) {
                        const auto& r = app.layoutBitmaps.back().destRect;
                        imageRowFirst = bitmapsBefore;
                        imageRowLinks = app.linkRects.size();
                        imageRowTop = r.top;
                        imageRowBottom = r.bottom;
                        imageRowRight = r.right;
                    }
                }
                yAfterImage = y;
                // An image inside a link (a README badge) clicks through like its text would
                if (isLink && app.layoutBitmaps.size() > bitmapsBefore) {
                    App::LinkRect lr;
                    lr.bounds = app.layoutBitmaps.back().destRect;
                    lr.url = linkUrl;
                    app.linkRects.push_back(lr);
                }
                continue;
            }

            case ElementType::Ruby: {
                // Collect base text and ruby annotation text
                std::wstring baseText, rubyText;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::RubyText) {
                        for (const auto& rtChild : child->children) {
                            if (rtChild->type == ElementType::Text) {
                                rubyText += toWide(rtChild->text);
                            }
                        }
                    } else if (child->type == ElementType::Text) {
                        baseText += toWide(child->text);
                    }
                }
                if (baseText.empty()) continue;

                float rubyFontSize = baseFormat->GetFontSize() * 0.5f;
                float rubyLineHeight = rubyFontSize * 1.4f;

                // Measure base text
                size_t rubyDocStart = app.docText.size();
                LayoutInfo baseInfo = createLayout(app, baseText, baseFormat, lineHeight, app.bodyTypography);
                float baseWidth = baseInfo.width;

                // Measure ruby text
                LayoutInfo rubyInfo;
                float rubyWidth = 0.0f;
                if (!rubyText.empty()) {
                    rubyInfo = createLayout(app, rubyText, baseFormat, rubyLineHeight, app.bodyTypography);
                    if (rubyInfo.layout) {
                        // Set smaller font size on the ruby layout. ON THE SEAM as of
                        // 2026-09-15, and its old comment is worth recording because
                        // it named its own unblocking condition and was right: it said
                        // "what gates this site is createLayout ... SetTypography takes
                        // an IDWriteTypography COM object ... converting this needs the
                        // typography question answered first." The typography question
                        // was answered, and the conversion is these three lines --
                        // DWRITE_TEXT_RANGE to tinta::TextRange, DWRITE_TEXT_METRICS to
                        // tinta::TextMetrics, same field names either side.
                        tinta::TextRange range = {0, (uint32_t)rubyText.length()};
                        rubyInfo.layout->SetFontSize(rubyFontSize, range);
                        tinta::TextMetrics metrics{};
                        rubyInfo.layout->GetMetrics(&metrics);
                        rubyWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                }

                float totalWidth = std::max(baseWidth, rubyWidth);

                // Word-wrap: treat ruby as atomic
                if (x + totalWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                // We need extra space above for the ruby text
                float rubyAboveOffset = rubyLineHeight;
                // If we're at the start of a line, push y down to make room for annotation
                // For simplicity, always reserve space above
                float baseY = y + mathTextOffset + rubyAboveOffset;

                // Center the narrower one under the wider one
                float basePosX = x + (totalWidth - baseWidth) / 2.0f;
                float rubyPosX = x + (totalWidth - rubyWidth) / 2.0f;

                // Draw ruby annotation (above base text, not selectable)
                if (rubyInfo.layout) {
                    tinta::Color rubyColor = baseColor;
                    rubyColor.a *= 0.7f;
                    tinta::PointF rubyPos = tinta::pointF(rubyPosX, y + mathTextOffset);
                    tinta::RectF rubyBounds = tinta::rectF(rubyPosX, y + mathTextOffset, rubyPosX + rubyWidth, y + mathTextOffset + rubyLineHeight);
                    addTextRun(app, std::move(rubyInfo), rubyPos, rubyBounds, rubyColor, 0, 0, false);
                }

                // Draw base text (selectable)
                tinta::PointF basePos = tinta::pointF(basePosX, baseY);
                tinta::RectF baseBounds = tinta::rectF(basePosX, baseY, basePosX + baseWidth, baseY + lineHeight);
                addTextRun(app, std::move(baseInfo), basePos, baseBounds, baseColor,
                           rubyDocStart, baseText.length(), true);

                app.docText += baseText;
                x += totalWidth + spaceWidth;

                // Adjust y to account for the extra ruby height on the next line wrap
                // The total height is rubyAboveOffset + lineHeight but we only advance by lineHeight
                // at the end of the line, so we need to make sure the ruby doesn't overlap
                continue;
            }

            default:
                continue;  // flattenInline only ever hands us leaves
        }

        if (text.empty()) continue;
        // The whitespace between two images is the gap the row already leaves
        if (x == startX && y == yAfterImage &&
            text.find_first_not_of(L' ') == std::wstring::npos) continue;

        size_t textDocStart = app.docText.size();
        float linkLineStartX = x;
        float linkLineY = y;

        // Measure the whole element once: cluster metrics give every break
        // unit's width without creating one IDWriteTextLayout per unit.
        // The measurement layout is kept alive: when the element ends up as
        // a single segment (no wrap — the common case), it IS the segment's
        // layout and no second CreateTextLayout happens.
        std::vector<float> cumW(text.length() + 1, -1.0f);
        std::vector<bool> isBoundary(text.length() + 1, false);
        cumW[0] = 0.0f;
        isBoundary[0] = true;
        isBoundary[text.length()] = true;
        LayoutInfo measureInfo = createLayout(app, text, format, lineHeight, app.bodyTypography);
        {
            if (measureInfo.layout) {
                // The first call is a COUNT PROBE, and its return value is ignored
                // deliberately: a probe reports FAILURE while still yielding the
                // count, on both backends. That is not an assumption about
                // DirectWrite's E_NOT_SUFFICIENT_BUFFER leaking through the seam --
                // tests/render_api_shared.cpp asserts it against both
                // implementations by name ("reports FAILURE while yielding the
                // count ... the two backends reach it by different routes and must
                // still agree"), because a backend that returned false WITHOUT
                // setting the count would leave every width in cumW at zero here
                // and nothing would error.
                uint32_t clusterCount = 0;
                measureInfo.layout->GetClusterMetrics(nullptr, 0, &clusterCount);
                if (clusterCount > 0) {
                    std::vector<tinta::ClusterMetrics> clusters(clusterCount);
                    if (measureInfo.layout->GetClusterMetrics(
                            clusters.data(), clusterCount, &clusterCount)) {
                        size_t cpos = 0;
                        float w = 0.0f;
                        for (uint32_t ci = 0; ci < clusterCount && cpos < text.length(); ci++) {
                            w += clusters[ci].width;
                            cpos += clusters[ci].length;
                            if (cpos <= text.length()) {
                                cumW[cpos] = w;
                                isBoundary[cpos] = true;
                            }
                        }
                    }
                }
            }
            // Fill positions that fall inside clusters (break opportunities
            // never land there)
            float last = 0.0f;
            for (auto& v : cumW) { if (v < 0.0f) v = last; else last = v; }
        }
        auto widthOf = [&cumW](size_t a, size_t b) { return cumW[b] - cumW[a]; };

        // Words on the same line merge into one drawn layout; per-word rects
        // are still recorded so hit-testing/selection/search stay word-level.
        struct WordRef { size_t start, len; };
        std::vector<WordRef> segWords;
        size_t segStart = 0;
        float segX = 0.0f;
        bool segOpen = false;
        float lastWordEndX = linkLineStartX;

        auto flushSegment = [&](size_t segEnd) {
            if (!segOpen) return;
            segOpen = false;
            if (segEnd <= segStart) { segWords.clear(); return; }
            std::wstring_view segText(text.data() + segStart, segEnd - segStart);

            // Single-segment element: the measurement layout already shaped
            // exactly this text (a space-only tail draws nothing) — hand it
            // over instead of shaping a second time
            bool coversAll = segStart == 0 && measureInfo.layout != nullptr;
            for (size_t i = segEnd; coversAll && i < text.length(); i++) {
                if (text[i] != L' ') coversAll = false;
            }
            LayoutInfo info;
            if (coversAll) {
                // A MOVE, where this was an assignment followed by nulling the
                // source field by hand to signal the transfer. Same effect, except
                // that the old pair could be separated by an edit and the move
                // cannot: measureInfo.layout is empty afterwards because the
                // shared_ptr says so, not because the next line remembered to.
                info.layout = std::move(measureInfo.layout);
                info.width = measureInfo.width;
                info.height = measureInfo.height;
            } else {
                info = createLayout(app, segText, format, lineHeight, app.bodyTypography);
            }
            float segWidth = widthOf(segStart, segEnd);
            if (hasBg) {
                app.layoutRects.push_back({
                    tinta::rectF(segX - 2, y + drawYOffset, segX + segWidth + 2,
                                y + drawYOffset + bgHeight),
                    bgColor});
            }
            if (hasStrike) {
                float strikeY = y + strikeOffset;
                app.layoutLines.push_back({tinta::pointF(segX, strikeY),
                                           tinta::pointF(segX + segWidth, strikeY),
                                           color, 1.0f});
            }
            tinta::PointF segPos = tinta::pointF(segX, y + drawYOffset);
            tinta::RectF segBounds = tinta::rectF(segX, y, segX + segWidth, y + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), segPos, segBounds, color,
                       textDocStart + segStart, segEnd - segStart, false);
            // Word rects hit-test against the covering segment layout
            size_t segRun = app.layoutTextRuns.size() > runsBefore
                ? app.layoutTextRuns.size() - 1 : (size_t)-1;
            for (const auto& w : segWords) {
                float wx = segX + widthOf(segStart, w.start);
                tinta::RectF wb = tinta::rectF(wx, y, wx + widthOf(w.start, w.start + w.len),
                                             y + lineHeight);
                addTextRect(app, wb, textDocStart + w.start, w.len, segRun);
            }
            segWords.clear();
        };

        // Break opportunities from the Unicode line breaking algorithm:
        // spaces for Latin text, between-ideograph positions (with proper
        // punctuation rules) for CJK, after / and - inside URLs, etc.
        std::vector<bool> canBreak;
        analyzeBreakOpportunities(app, text, canBreak);

        size_t pos = 0;
        size_t lastWordEnd = 0;

        auto emitWord = [&](size_t wordStart, size_t wordEnd) {
            float wordWidth = widthOf(wordStart, wordEnd);
            float wordX = segOpen ? segX + widthOf(segStart, wordStart) : x;

            if (wordX + wordWidth > maxX && wordX > startX) {
                // Unit wraps: flush the current line's segment first
                flushSegment(lastWordEnd);
                if (isLink && lastWordEndX > linkLineStartX) {
                    addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color,
                                   format, linkLineY + drawYOffset);
                }
                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
                wordX = x;
            }

            if (!segOpen) {
                segOpen = true;
                segStart = wordStart;
                segX = wordX;
            }
            segWords.push_back({wordStart, wordEnd - wordStart});
            lastWordEnd = wordEnd;
            lastWordEndX = segX + widthOf(segStart, wordEnd);
            x = lastWordEndX;
        };

        while (pos < text.length()) {
            // The next unit runs to the following break opportunity
            size_t bp = pos + 1;
            while (bp < text.length() && !canBreak[bp]) bp++;

            // Trailing spaces belong to the unit but don't participate in
            // the wrap decision and never render at a line start
            size_t vis = bp;
            while (vis > pos && text[vis - 1] == L' ') vis--;

            if (vis > pos) {
                if (widthOf(pos, vis) > maxWidth) {
                    // Emergency break: a single unbreakable unit wider than a
                    // whole line splits at cluster boundaries so nothing is
                    // ever clipped or overlaps a neighbor
                    size_t pieceStart = pos;
                    while (widthOf(pieceStart, vis) > maxWidth) {
                        size_t lo = pieceStart + 1, hi = vis - 1;
                        while (lo < hi) {
                            size_t mid = (lo + hi + 1) / 2;
                            if (widthOf(pieceStart, mid) <= maxWidth) lo = mid;
                            else hi = mid - 1;
                        }
                        size_t pieceEnd = lo;
                        while (pieceEnd > pieceStart + 1 && !isBoundary[pieceEnd]) pieceEnd--;
                        if (pieceEnd <= pieceStart) pieceEnd = pieceStart + 1;
                        emitWord(pieceStart, pieceEnd);
                        pieceStart = pieceEnd;
                        if (pieceStart >= vis) break;
                    }
                    if (pieceStart < vis) emitWord(pieceStart, vis);
                } else {
                    emitWord(pos, vis);
                }
            }

            if (bp > vis) {
                // Advance x across the trailing spaces (shaped width when a
                // segment is open)
                x = segOpen ? segX + widthOf(segStart, bp) : x + widthOf(vis, bp);
            }
            pos = bp;
        }
        flushSegment(lastWordEnd);
        // measureInfo.layout was Released here by hand. It is a shared_ptr now, so
        // whichever of the two things happened -- flushSegment moved it into a run,
        // or the element wrapped and it was only ever a measurement -- the layout
        // dies at the right time without this line deciding which case it was in.
        app.docText += text;

        if (isLink && lastWordEndX > linkLineStartX) {
            addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color,
                           format, linkLineY + drawYOffset);
        }
    }

    if (x == startX && (y == yAfterImage || y == yAfterBreak)) return;
    y += lineHeight;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

// Display math ($$...$$, #80): a centered block rendered natively; the raw
// TeX joins docText so search and selection see the source
static void layoutMathBlock(App& app, const ElementPtr& elem, float& y,
                            float indent, float maxWidth) {
    std::wstring latex = toWide(elem->text);
    float baseSize = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    MathBoxPtr box = mathParse(app, latex, baseSize * 1.06f, true);
    if (!box) {
        // Unsupported TeX: raw source in code style (the mermaid pattern)
        auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
        auto text = std::make_shared<Element>(ElementType::Text);
        text->text = elem->text;
        text->parent = fallback.get();
        fallback->children.push_back(std::move(text));
        fallback->sourceOffset = elem->sourceOffset;
        layoutCodeBlock(app, fallback, y, indent, maxWidth);
        return;
    }
    float scale = app.contentScale * app.zoomFactor;
    y += 6 * scale;
    float w = mathBoxWidth(box);
    float x = indent + std::max(0.0f, (maxWidth - w) / 2);
    mathBoxRetain(app, box, x, y, app.theme.text);

    size_t docStart = app.docText.size();
    app.docText += latex;
    addTextRect(app, tinta::rectF(x, y, x + w, y + mathBoxHeight(box)),
                docStart, latex.size());
    app.docText += L"\n\n";

    y += mathBoxHeight(box) + 14 * scale;
}

// A paragraph that is exactly one $$...$$ span renders as a math block
static const ElementPtr* soleMathDisplayChild(const ElementPtr& elem) {
    const ElementPtr* found = nullptr;
    for (const auto& child : elem->children) {
        if (!child) continue;
        if (child->type == ElementType::MathDisplay) {
            if (found) return nullptr;
            found = &child;
        } else if (child->type == ElementType::Text) {
            for (char c : child->text) {
                if (!isspace((unsigned char)c)) return nullptr;
            }
        } else if (child->type != ElementType::SoftBreak) {
            return nullptr;
        }
    }
    return found;
}

static void layoutParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (const ElementPtr* math = soleMathDisplayChild(elem)) {
        layoutMathBlock(app, *math, y, indent, maxWidth);
        return;
    }
    LayoutSnapshot start = takeSnapshot(app);
    layoutInlineContent(app, elem->children, indent, y, maxWidth, app.textFormat, app.theme.text);
    alignLaidOutLines(app, start, indent, maxWidth, elem->align);
    app.docText += L"\n\n";
    float scale = app.contentScale * app.zoomFactor;
    y += 14 * scale;
}

static void layoutHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    int levelIndex = std::min(elem->level - 1, 5);
    tinta::TextFormat* format = app.headingFormats[levelIndex] ? app.headingFormats[levelIndex] : app.textFormat;

    if (elem->level == 1) {
        y += 16 * scale;
    } else {
        y += 20 * scale;
    }

    // Record heading for TOC (h1-h3 only)
    if (elem->level <= 3) {
        std::wstring headingText;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) headingText += toWide(e->text);
            else for (const auto& c : e->children) extract(c);
        };
        for (const auto& child : elem->children) extract(child);

        std::string baseId = slugifyHeading(headingText);
        int& n = app.headingSlugCounts[baseId];
        std::string id = (n == 0) ? baseId : (baseId + "-" + std::to_string(n));
        n++;
        app.headings.push_back({headingText, elem->level, y, id});
    }

    LayoutSnapshot start = takeSnapshot(app);
    layoutInlineContent(app, elem->children, indent, y, maxWidth, format, app.theme.heading);
    alignLaidOutLines(app, start, indent, maxWidth, elem->align);

    if (elem->level <= 2 && app.headingRules) {
        y += 6 * scale;
        tinta::Color lineColor = app.theme.heading;
        lineColor.a = 0.3f;
        float lineWidth = (elem->level == 1) ? 2.0f * scale : 1.0f * scale;
        app.layoutLines.push_back({tinta::pointF(indent, y),
                                   tinta::pointF(indent + maxWidth, y),
                                   lineColor, lineWidth});
        y += lineWidth;
    }

    app.docText += L"\n\n";
    y += 12 * scale;
}

// The mermaid label layout, and the same collapse as createLayout: the native
// overload had no callers of its own -- all three sites passed app.textFormat -- so
// two functions become one. This one applies no typography, which is not an omission
// but the behaviour it always had; diagram labels have never carried font features.
static LayoutInfo createWrappedLayout(App& app, std::wstring_view text,
                                      tinta::TextFormat* format, float width,
                                      float height) {
    LayoutInfo info;
    if (!format || text.empty() || width <= 0.0f || height <= 0.0f ||
        !app.textFactory) {
        return info;
    }

    info.layout = app.textFactory->CreateTextLayout(
        text.data(), static_cast<uint32_t>(text.length()), format, width, height);
    if (!info.layout) return info;

    info.layout->SetWordWrapping(tinta::WordWrapping::Wrap);
    info.layout->SetTextAlignment(tinta::TextAlignment::Center);
    info.layout->SetParagraphAlignment(tinta::ParagraphAlignment::Center);
    applyEmojiFallback(app, info.layout.get());

    tinta::TextMetrics metrics{};
    info.layout->GetMetrics(&metrics);
    info.width = metrics.widthIncludingTrailingWhitespace;
    info.height = metrics.height;
    return info;
}

static tinta::Color mermaidColor(const mermaid::Color& color) {
    return tinta::colorF(
        ((color.rgb >> 16) & 0xFF) / 255.0f,
        ((color.rgb >> 8) & 0xFF) / 255.0f,
        (color.rgb & 0xFF) / 255.0f,
        color.alpha);
}

struct ResolvedMermaidStyle {
    tinta::Color fill{};
    tinta::Color stroke{};
    tinta::Color text{};
    float strokeWidth = 1.0f;
};

static ResolvedMermaidStyle resolveMermaidStyle(
        const App& app, const mermaid::Diagram& diagram,
        const mermaid::Node& node, float scale) {
    ResolvedMermaidStyle resolved;
    resolved.fill = app.theme.codeBackground;
    resolved.stroke = app.theme.accent;
    resolved.text = app.theme.text;
    resolved.strokeWidth = 1.5f * scale;

    auto apply = [&](const mermaid::Style& style) {
        if (style.hasFill) resolved.fill = mermaidColor(style.fill);
        if (style.hasStroke) resolved.stroke = mermaidColor(style.stroke);
        if (style.hasText) resolved.text = mermaidColor(style.text);
        if (style.hasStrokeWidth) {
            resolved.strokeWidth = style.strokeWidth * scale;
        }
    };

    auto defaultStyle = diagram.classStyles.find("default");
    if (defaultStyle != diagram.classStyles.end()) apply(defaultStyle->second);
    if (!node.className.empty()) {
        auto classStyle = diagram.classStyles.find(node.className);
        if (classStyle != diagram.classStyles.end()) apply(classStyle->second);
    }
    apply(node.style);
    return resolved;
}

static App::LayoutShapeType mermaidShapeType(mermaid::NodeShape shape) {
    switch (shape) {
        case mermaid::NodeShape::RoundedRectangle:
            return App::LayoutShapeType::RoundedRectangle;
        case mermaid::NodeShape::Diamond:
            return App::LayoutShapeType::Diamond;
        case mermaid::NodeShape::Stadium:
            return App::LayoutShapeType::Stadium;
        case mermaid::NodeShape::Circle:
            return App::LayoutShapeType::Ellipse;
        case mermaid::NodeShape::Hexagon:
            return App::LayoutShapeType::Hexagon;
        case mermaid::NodeShape::Rectangle:
        default:
            return App::LayoutShapeType::Rectangle;
    }
}

static bool layoutMermaidDiagram(App& app, const std::string& source,
                                 size_t sourceOffset, float& y,
                                 float indent, float maxWidth,
                                 tinta::RectF* renderedBounds = nullptr,
                                 float scaleMul = 1.0f) {
    auto parsed = mermaid::parse(source);
    if (!parsed.success || parsed.diagram.nodes.empty()) return false;

    const auto& diagram = parsed.diagram;
    float scale = app.contentScale * app.zoomFactor * scaleMul;
    float maxLabelWidth = 280.0f * scale;
    float measureHeight = 10000.0f * scale;
    float paddingX = 18.0f * scale;
    float paddingY = 12.0f * scale;
    float minimumWidth = 120.0f * scale;
    float minimumHeight = 52.0f * scale;

    std::vector<std::wstring> labels;
    std::vector<mermaid::Size> nodeSizes;
    std::vector<ResolvedMermaidStyle> styles;
    labels.reserve(diagram.nodes.size());
    nodeSizes.reserve(diagram.nodes.size());
    styles.reserve(diagram.nodes.size());

    for (const auto& node : diagram.nodes) {
        std::wstring label = toWide(node.label.empty() ? node.id : node.label);
        LayoutInfo measured = createWrappedLayout(
            app, label, app.textFormat, maxLabelWidth, measureHeight);
        float width = std::max(minimumWidth, measured.width + paddingX * 2.0f);
        float height = std::max(minimumHeight, measured.height + paddingY * 2.0f);
        // `measured` is shaped only to read .width and .height off it and is dropped
        // at the end of the iteration. The explicit Release() that used to be here is
        // the one this loop could most easily have leaked past a `continue`.

        if (node.shape == mermaid::NodeShape::Diamond) {
            width = std::max(width * 1.28f, 150.0f * scale);
            height = std::max(height * 1.45f, 82.0f * scale);
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            width += 40.0f * scale;
        } else if (node.shape == mermaid::NodeShape::Circle) {
            float diameter = std::max(width, height);
            width = diameter;
            height = diameter;
        }

        labels.push_back(std::move(label));
        nodeSizes.push_back({width, height});
        styles.push_back(resolveMermaidStyle(app, diagram, node, scale));
    }

    struct MeasuredMermaidEdgeLabel {
        std::wstring text;
        float width = 0.0f;
        float height = 0.0f;
    };
    bool vertical = diagram.direction == mermaid::Direction::TopToBottom ||
                    diagram.direction == mermaid::Direction::BottomToTop;
    float labelPaddingX = 6.0f * scale;
    float labelPaddingY = 4.0f * scale;
    float rankGap = 78.0f * scale;
    std::vector<MeasuredMermaidEdgeLabel> edgeLabels(diagram.edges.size());
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        if (diagram.edges[i].label.empty()) continue;

        auto& edgeLabel = edgeLabels[i];
        edgeLabel.text = toWide(diagram.edges[i].label);
        edgeLabel.width = std::min(
            180.0f * scale,
            std::max(60.0f * scale,
                     measureText(app, edgeLabel.text, app.textFormat) +
                         labelPaddingX * 2.0f));
        LayoutInfo measured = createWrappedLayout(
            app, edgeLabel.text, app.textFormat,
            edgeLabel.width - labelPaddingX * 2.0f, measureHeight);
        edgeLabel.height = std::max(
            28.0f * scale, measured.height + labelPaddingY * 2.0f);
        // Measurement only, as above -- dropped with the loop iteration.

        float labelExtent = vertical ? edgeLabel.height : edgeLabel.width;
        rankGap = std::max(rankGap, labelExtent + 20.0f * scale);
    }

    mermaid::Layout graphLayout = mermaid::layout(
        diagram, nodeSizes, 32.0f * scale, rankGap);
    if (graphLayout.nodes.size() != diagram.nodes.size()) return false;

    float baseX = indent;
    if (graphLayout.width < maxWidth) {
        baseX += (maxWidth - graphLayout.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;
    if (!diagram.subgraphs.empty()) {
        // Group boxes extend above their topmost node by pad + title
        // height; reserve that room so they stay inside the block
        baseY += 30.0f * scale;
    }
    float diagramLeft = baseX;
    float diagramTop = baseY;
    float diagramRight = baseX + graphLayout.width;
    float diagramBottom = baseY + graphLayout.height;

    std::vector<tinta::RectF> nodeRects;
    nodeRects.reserve(graphLayout.nodes.size());
    for (const auto& rect : graphLayout.nodes) {
        nodeRects.push_back(tinta::rectF(
            baseX + rect.left,
            baseY + rect.top,
            baseX + rect.right,
            baseY + rect.bottom));
    }

    struct MermaidTextItem {
        std::wstring text;
        tinta::RectF rect{};
        tinta::Color color{};
    };
    std::vector<MermaidTextItem> textItems;
    textItems.reserve(diagram.nodes.size() + diagram.edges.size());

    tinta::Color connectorColor = app.theme.text;
    connectorColor.a = app.theme.isDark ? 0.7f : 0.6f;

    // Subgraph group boxes: fill goes through layoutRects (drawn before the
    // connectors), the border rides with the node shapes, the title with the
    // node labels. Children were declared after their parents, so a reverse
    // pass finalizes leaves first and parents grow around them.
    if (!diagram.subgraphs.empty()) {
        float boxPad = 10.0f * scale;
        float titleHeight = 20.0f * scale;
        constexpr float kEmpty = std::numeric_limits<float>::max();
        std::vector<tinta::RectF> boxes(
            diagram.subgraphs.size(),
            tinta::rectF(kEmpty, kEmpty, -kEmpty, -kEmpty));
        auto grow = [](tinta::RectF& box, const tinta::RectF& rect) {
            box.left = std::min(box.left, rect.left);
            box.top = std::min(box.top, rect.top);
            box.right = std::max(box.right, rect.right);
            box.bottom = std::max(box.bottom, rect.bottom);
        };
        for (size_t s = diagram.subgraphs.size(); s-- > 0;) {
            for (size_t node : diagram.subgraphs[s].nodes) {
                if (node < nodeRects.size()) grow(boxes[s], nodeRects[node]);
            }
            for (size_t c = s + 1; c < diagram.subgraphs.size(); c++) {
                if (diagram.subgraphs[c].parent == s &&
                    boxes[c].left != kEmpty) {
                    grow(boxes[s], boxes[c]);
                }
            }
            if (boxes[s].left == kEmpty) continue;
            boxes[s].left -= boxPad;
            boxes[s].top -= boxPad + titleHeight;
            boxes[s].right += boxPad;
            boxes[s].bottom += boxPad;
        }
        tinta::Color groupFill = app.theme.codeBackground;
        groupFill.a *= 0.55f;
        tinta::Color groupBorder = app.theme.text;
        groupBorder.a = 0.28f;
        tinta::Color groupTitle = app.theme.text;
        groupTitle.a = 0.78f;
        for (size_t s = 0; s < diagram.subgraphs.size(); s++) {
            if (boxes[s].left == kEmpty) continue;
            app.layoutRects.push_back({boxes[s], groupFill});
            app.layoutShapes.push_back({
                App::LayoutShapeType::Rectangle,
                boxes[s],
                tinta::colorF(0, 0, 0, 0),
                groupBorder,
                1.2f * scale,
                0.0f,
                nullptr,  // geometry: built on first draw
            });
            tinta::RectF titleRect = tinta::rectF(
                boxes[s].left + 8.0f * scale, boxes[s].top + 2.0f * scale,
                boxes[s].right - 8.0f * scale,
                boxes[s].top + titleHeight + 2.0f * scale);
            textItems.push_back({toWide(diagram.subgraphs[s].label),
                                 titleRect, groupTitle});
            diagramLeft = std::min(diagramLeft, boxes[s].left);
            diagramTop = std::min(diagramTop, boxes[s].top);
            diagramRight = std::max(diagramRight, boxes[s].right);
            diagramBottom = std::max(diagramBottom, boxes[s].bottom);
        }
    }

    app.layoutConnectors.reserve(
        app.layoutConnectors.size() + diagram.edges.size());
    size_t exteriorLane = 0;
    std::vector<tinta::RectF> placedLabelRects;
    placedLabelRects.reserve(diagram.edges.size());
    for (size_t edgeIndex = 0; edgeIndex < diagram.edges.size(); edgeIndex++) {
        const auto& edge = diagram.edges[edgeIndex];
        if (edge.from >= nodeRects.size() || edge.to >= nodeRects.size()) continue;

        const auto& from = nodeRects[edge.from];
        const auto& to = nodeRects[edge.to];
        App::LayoutConnector connector;
        connector.color = connectorColor;
        connector.stroke = 1.4f * scale * edge.strokeScale;
        connector.arrowSize = 8.0f * scale;
        connector.directed = edge.directed;
        connector.dashed = edge.dashed;

        float fromCenterX = (from.left + from.right) * 0.5f;
        float fromCenterY = (from.top + from.bottom) * 0.5f;
        float toCenterX = (to.left + to.right) * 0.5f;
        float toCenterY = (to.top + to.bottom) * 0.5f;
        bool selfLoop = edge.from == edge.to;

        // Edges that skip over intermediate ranks would cut straight through
        // the nodes between them (and drop their label onto whatever edge
        // happens to sit at the midpoint) — route those through an exterior
        // lane like back-edges instead
        bool skipsRanks = false;
        if (edge.from < graphLayout.ranks.size() &&
            edge.to < graphLayout.ranks.size()) {
            size_t fromRank = graphLayout.ranks[edge.from];
            size_t toRank = graphLayout.ranks[edge.to];
            skipsRanks = (fromRank < toRank ? toRank - fromRank
                                            : fromRank - toRank) > 1;
        }

        if (vertical) {
            bool topToBottom =
                diagram.direction == mermaid::Direction::TopToBottom;
            bool forward = !selfLoop && !skipsRanks &&
                (topToBottom ? toCenterY > fromCenterY : toCenterY < fromCenterY);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                tinta::PointF start = tinta::pointF(from.right, fromCenterY);
                tinta::PointF end = tinta::pointF(fromCenterX, from.bottom);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    tinta::pointF(laneX, start.y),
                    tinta::pointF(laneX, laneY),
                    tinta::pointF(end.x, laneY),
                    end,
                };
            } else if (forward) {
                tinta::PointF start = tinta::pointF(
                    fromCenterX, topToBottom ? from.bottom : from.top);
                tinta::PointF end = tinta::pointF(
                    toCenterX, topToBottom ? to.top : to.bottom);
                float middleY = (start.y + end.y) * 0.5f;
                connector.points = {
                    start,
                    tinta::pointF(start.x, middleY),
                    tinta::pointF(end.x, middleY),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                tinta::PointF start = tinta::pointF(from.right, fromCenterY);
                tinta::PointF end = tinta::pointF(to.right, toCenterY);
                float laneX = std::max(from.right, to.right) + lane;
                connector.points = {
                    start,
                    tinta::pointF(laneX, start.y),
                    tinta::pointF(laneX, end.y),
                    end,
                };
            }
        } else {
            bool leftToRight =
                diagram.direction == mermaid::Direction::LeftToRight;
            bool forward = !selfLoop && !skipsRanks &&
                (leftToRight ? toCenterX > fromCenterX : toCenterX < fromCenterX);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                tinta::PointF start = tinta::pointF(fromCenterX, from.bottom);
                tinta::PointF end = tinta::pointF(from.right, fromCenterY);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    tinta::pointF(start.x, laneY),
                    tinta::pointF(laneX, laneY),
                    tinta::pointF(laneX, end.y),
                    end,
                };
            } else if (forward) {
                tinta::PointF start = tinta::pointF(
                    leftToRight ? from.right : from.left, fromCenterY);
                tinta::PointF end = tinta::pointF(
                    leftToRight ? to.left : to.right, toCenterY);
                float middleX = (start.x + end.x) * 0.5f;
                connector.points = {
                    start,
                    tinta::pointF(middleX, start.y),
                    tinta::pointF(middleX, end.y),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                tinta::PointF start = tinta::pointF(fromCenterX, from.bottom);
                tinta::PointF end = tinta::pointF(toCenterX, to.bottom);
                float laneY = std::max(from.bottom, to.bottom) + lane;
                connector.points = {
                    start,
                    tinta::pointF(start.x, laneY),
                    tinta::pointF(end.x, laneY),
                    end,
                };
            }
        }

        connector.bounds = tinta::rectF(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());
        for (const auto& point : connector.points) {
            connector.bounds.left = std::min(connector.bounds.left, point.x);
            connector.bounds.top = std::min(connector.bounds.top, point.y);
            connector.bounds.right = std::max(connector.bounds.right, point.x);
            connector.bounds.bottom = std::max(connector.bounds.bottom, point.y);
        }
        connector.bounds.left -= connector.arrowSize;
        connector.bounds.top -= connector.arrowSize;
        connector.bounds.right += connector.arrowSize;
        connector.bounds.bottom += connector.arrowSize;
        diagramLeft = std::min(diagramLeft, connector.bounds.left);
        diagramTop = std::min(diagramTop, connector.bounds.top);
        diagramRight = std::max(diagramRight, connector.bounds.right);
        diagramBottom = std::max(diagramBottom, connector.bounds.bottom);
        app.layoutConnectors.push_back(std::move(connector));

        if (!edge.label.empty()) {
            const auto& edgeLabel = edgeLabels[edgeIndex];
            const auto& points = app.layoutConnectors.back().points;
            size_t middle = points.size() / 2;
            const auto& middleStart = points[middle - 1];
            const auto& middleEnd = points[middle];
            float centerX = (middleStart.x + middleEnd.x) * 0.5f;
            float centerY = (middleStart.y + middleEnd.y) * 0.5f;
            auto chipAt = [&](float cx, float cy) {
                return tinta::rectF(
                    cx - edgeLabel.width * 0.5f,
                    cy - edgeLabel.height * 0.5f,
                    cx + edgeLabel.width * 0.5f,
                    cy + edgeLabel.height * 0.5f);
            };
            tinta::RectF labelRect = chipAt(centerX, centerY);

            // Parallel exterior lanes sit only a few pixels apart, so their
            // midpoint chips stack on top of each other — slide an
            // overlapping chip along its own segment until it finds space
            bool segVertical = std::abs(middleEnd.x - middleStart.x) <
                               std::abs(middleEnd.y - middleStart.y);
            float stepX = segVertical ? 0.0f : edgeLabel.width + 8.0f * scale;
            float stepY = segVertical ? edgeLabel.height + 8.0f * scale : 0.0f;
            auto overlapsPlaced = [&](const tinta::RectF& rect) {
                for (const auto& placed : placedLabelRects) {
                    if (rect.left < placed.right && rect.right > placed.left &&
                        rect.top < placed.bottom && rect.bottom > placed.top) {
                        return true;
                    }
                }
                return false;
            };
            for (int attempt = 1; attempt <= 8 && overlapsPlaced(labelRect); attempt++) {
                float direction = (attempt % 2 == 1) ? 1.0f : -1.0f;
                float magnitude = (float)((attempt + 1) / 2);
                labelRect = chipAt(centerX + stepX * direction * magnitude,
                                   centerY + stepY * direction * magnitude);
            }
            placedLabelRects.push_back(labelRect);
            // Draw the label as a visible chip on the edge — an invisible
            // background-colored pill erases the line under it, which makes
            // edges look disconnected and labels look like floating text
            tinta::Color chipStroke = connectorColor;
            chipStroke.a *= 0.6f;
            app.layoutShapes.push_back({
                App::LayoutShapeType::RoundedRectangle,
                labelRect,
                app.theme.codeBackground,
                chipStroke,
                1.2f * scale,
                4.0f * scale,
                nullptr,  // geometry: built on first draw
            });
            diagramLeft = std::min(diagramLeft, labelRect.left);
            diagramTop = std::min(diagramTop, labelRect.top);
            diagramRight = std::max(diagramRight, labelRect.right);
            diagramBottom = std::max(diagramBottom, labelRect.bottom);
            tinta::RectF textRect = tinta::rectF(
                labelRect.left + labelPaddingX,
                labelRect.top + labelPaddingY,
                labelRect.right - labelPaddingX,
                labelRect.bottom - labelPaddingY);
            textItems.push_back({edgeLabel.text, textRect, app.theme.text});
        }
    }

    app.layoutShapes.reserve(app.layoutShapes.size() + diagram.nodes.size());
    for (size_t i = 0; i < diagram.nodes.size(); i++) {
        const auto& node = diagram.nodes[i];
        const auto& rect = nodeRects[i];
        const auto& style = styles[i];

        app.layoutShapes.push_back({
            mermaidShapeType(node.shape),
            rect,
            style.fill,
            style.stroke,
            style.strokeWidth,
            8.0f * scale,
            nullptr,  // geometry: built on first draw
        });

        float insetX = paddingX;
        float insetY = paddingY;
        if (node.shape == mermaid::NodeShape::Diamond) {
            insetX = (rect.right - rect.left) * 0.18f;
            insetY = (rect.bottom - rect.top) * 0.18f;
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            insetX = (rect.right - rect.left) * 0.18f;
        }

        tinta::RectF textRect = tinta::rectF(
            rect.left + insetX,
            rect.top + insetY,
            rect.right - insetX,
            rect.bottom - insetY);
        textItems.push_back({labels[i], textRect, style.text});

        if (sourceOffset != SIZE_MAX) {
            size_t anchorOffset = sourceOffset + node.sourceOffset;
            if (app.scrollAnchors.empty() ||
                app.scrollAnchors.back().sourceOffset < anchorOffset) {
                app.scrollAnchors.push_back({anchorOffset, rect.top});
            }
        }
    }

    std::stable_sort(
        textItems.begin(), textItems.end(),
        [](const MermaidTextItem& left, const MermaidTextItem& right) {
            if (std::abs(left.rect.top - right.rect.top) > kLineBucketTolerance) {
                return left.rect.top < right.rect.top;
            }
            return left.rect.left < right.rect.left;
        });
    for (const auto& item : textItems) {
        LayoutInfo textLayout = createWrappedLayout(
            app, item.text, app.textFormat,
            item.rect.right - item.rect.left,
            item.rect.bottom - item.rect.top);
        size_t docStart = app.docText.size();
        app.docText += item.text;
        addTextRun(
            app, std::move(textLayout),
            tinta::pointF(item.rect.left, item.rect.top),
            item.rect, item.color, docStart, item.text.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    app.contentWidth = std::max(
        app.contentWidth,
        diagramRight + 40.0f * scale);
    if (app.focusMermaidOnNextLayout && !nodeRects.empty()) {
        std::vector<bool> hasIncoming(diagram.nodes.size(), false);
        for (const auto& edge : diagram.edges) {
            if (edge.to < hasIncoming.size()) hasIncoming[edge.to] = true;
        }
        size_t rootIndex = 0;
        for (size_t i = 0; i < hasIncoming.size(); i++) {
            if (!hasIncoming[i]) {
                rootIndex = i;
                break;
            }
        }

        float rootCenter = (nodeRects[rootIndex].left + nodeRects[rootIndex].right) * 0.5f;
        float viewportWidth = documentViewportWidth(app);
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        float focusedScroll = rootCenter - viewportWidth * 0.5f;
        app.scrollX = std::max(0.0f, std::min(focusedScroll, maxScroll));
        app.targetScrollX = app.scrollX;
        app.focusMermaidOnNextLayout = false;
    }
    if (renderedBounds) {
        *renderedBounds =
            tinta::rectF(diagramLeft, diagramTop, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

// --- extended Mermaid diagram families (sequence, pie, state, ...) ---

static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxC = std::max(r, std::max(g, b));
    float minC = std::min(r, std::min(g, b));
    l = (maxC + minC) * 0.5f;
    if (maxC == minC) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    float d = maxC - minC;
    s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);
    if (maxC == r) {
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    } else if (maxC == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h /= 6.0f;
}

static float hueChannel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) {
        r = g = b = l;
        return;
    }
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hueChannel(p, q, h + 1.0f / 3.0f);
    g = hueChannel(p, q, h);
    b = hueChannel(p, q, h - 1.0f / 3.0f);
}

// Categorical palette derived from the theme accent: golden-angle hue steps
// keep neighboring series distinct in any theme
static tinta::Color diagramSeriesColor(const App& app, int index) {
    float h, s, l;
    rgbToHsl(app.theme.accent.r, app.theme.accent.g, app.theme.accent.b,
             h, s, l);
    h = std::fmod(h + index * 0.38197f, 1.0f);
    s = app.theme.isDark ? 0.42f : 0.48f;
    l = app.theme.isDark ? 0.60f : 0.52f;
    float r, g, b;
    hslToRgb(h, s, l, r, g, b);
    return tinta::colorF(r, g, b, 1.0f);
}

tinta::Color resolveDiagramRoleImpl(const App& app,
                                const mermaidext::Prim& prim,
                                mermaidext::Role role) {
    int seriesIndex = prim.seriesIndex;
    tinta::Color color = app.theme.text;
    switch (role) {
        case mermaidext::Role::Text:
            color = app.theme.text;
            break;
        case mermaidext::Role::Muted:
            color = app.theme.text;
            color.a = app.theme.isDark ? 0.72f : 0.62f;
            break;
        case mermaidext::Role::Stroke:
            color = app.theme.accent;
            break;
        case mermaidext::Role::Fill:
            color = app.theme.codeBackground;
            break;
        case mermaidext::Role::Accent:
            color = app.theme.accent;
            break;
        case mermaidext::Role::AccentSoft:
            color = app.theme.accent;
            color.a = app.theme.isDark ? 0.20f : 0.13f;
            break;
        case mermaidext::Role::Background:
            color = app.theme.background;
            break;
        case mermaidext::Role::Series:
            color = diagramSeriesColor(app, seriesIndex);
            break;
        case mermaidext::Role::SeriesSoft:
            color = diagramSeriesColor(app, seriesIndex);
            color.a = 0.30f;
            break;
        case mermaidext::Role::Custom:
            color = tinta::colorF(prim.customR, prim.customG, prim.customB,
                                 prim.customA);
            break;
        case mermaidext::Role::None:
            color.a = 0.0f;
            break;
    }
    return color;
}

// Per-layout-pass cache of text formats for the diagram text styles.
//
// The formats are unique_ptrs to seam handles now, which deletes the destructor: the
// Release loop it replaced was correct, but it was the only thing standing between
// this cache and a leak, and a cache is exactly the shape where an early return gets
// added later. get() still returns a borrowed raw pointer, deliberately -- the map
// owns every format for the whole layout pass and no caller outlives it.
struct DiagramFormats {
    App& app;
    std::map<int, std::unique_ptr<tinta::TextFormat>> formats;

    explicit DiagramFormats(App& application) : app(application) {}
    DiagramFormats(const DiagramFormats&) = delete;
    DiagramFormats& operator=(const DiagramFormats&) = delete;

    tinta::TextFormat* get(const mermaidext::TextStyle& style, float scale) {
        int key = (style.bold ? 1 : 0) | (style.italic ? 2 : 0) |
                  (style.mono ? 4 : 0) |
                  (static_cast<int>(style.scale * 100.0f + 0.5f) << 3);
        auto found = formats.find(key);
        if (found != formats.end()) return found->second.get();

        if (!app.textFactory) return nullptr;
        const wchar_t* family =
            style.mono ? app.theme.codeFontFamily : app.theme.fontFamily;
        float size = (style.mono ? 13.0f : 14.0f) * scale * style.scale;
        std::unique_ptr<tinta::TextFormat> format = app.textFactory->CreateTextFormat(
            family,
            style.bold ? tinta::FontWeight::SemiBold : tinta::FontWeight::Normal,
            style.italic ? tinta::FontStyle::Italic : tinta::FontStyle::Normal,
            tinta::FontStretch::Normal, size, L"en-us");
        tinta::TextFormat* borrowed = format.get();
        // Cached even when creation FAILED, which is what the old code did by
        // storing a null IDWriteTextFormat*. Kept on purpose: it means a font family
        // the system cannot resolve is attempted once per layout pass rather than
        // once per diagram label.
        formats[key] = std::move(format);
        return borrowed;
    }
};

// Returns an OWNING handle where it used to return a raw IDWriteTextLayout* that both
// callers had to dispose of themselves -- one by Release(), the other by handing it to
// a LayoutInfo. That split is what made the raw return worth changing: two callers,
// two different disposal routes, and nothing in the signature saying so.
static std::shared_ptr<tinta::TextLayout> createDiagramTextLayout(
        App& app, DiagramFormats& formats, const std::wstring& text,
        const mermaidext::TextStyle& style, float scale, float width,
        float height, int alignH, int alignV) {
    tinta::TextFormat* format = formats.get(style, scale);
    if (!format || text.empty() || !app.textFactory) return nullptr;
    std::shared_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
        text.data(), static_cast<uint32_t>(text.size()), format,
        std::max(width, 1.0f), std::max(height, 1.0f));
    if (!layout) return nullptr;
    layout->SetTextAlignment(alignH < 0 ? tinta::TextAlignment::Leading
                             : alignH > 0 ? tinta::TextAlignment::Trailing
                                          : tinta::TextAlignment::Center);
    layout->SetParagraphAlignment(alignV < 0
                                      ? tinta::ParagraphAlignment::Near
                                  : alignV > 0 ? tinta::ParagraphAlignment::Far
                                               : tinta::ParagraphAlignment::Center);
    applyEmojiFallback(app, layout.get());
    return layout;
}

// The two path builders, on the seam. Both used to open an ID2D1GeometrySink on an
// ID2D1PathGeometry from app.d2dFactory and hand back a raw refcounted pointer that
// two hand-written loops elsewhere were responsible for releasing; both of those
// loops are deleted in the same commit (App::clearLayoutCache and rollbackTo above).
//
// Note what did NOT change: the call SEQUENCE is identical, figure for figure and
// arc for arc, including buildSliceGeometry's full-circle special case. That is
// deliberate. render_api.h records the alternative that was rejected -- a seam
// taking a DESCRIPTION of a slice rather than mirroring the sink -- because
// src/export_docx.cpp builds the same shape WITHOUT this special case, so a
// description-shaped seam would have silently fixed the DOCX export's full-circle
// bug under a commit whose subject line says portability. The divergence stays a
// named, separately fixable defect.
static std::unique_ptr<tinta::PathGeometry> buildPolygonGeometry(
        App& app, const std::vector<mermaidext::Point>& points,
        float originX, float originY) {
    if (points.size() < 3 || !app.renderTarget) return nullptr;
    std::unique_ptr<tinta::PathSink> sink = app.renderTarget->BeginPath();
    if (!sink) return nullptr;
    sink->BeginFigure(
        tinta::pointF(points[0].x - originX, points[0].y - originY));
    for (size_t i = 1; i < points.size(); i++) {
        sink->AddLine(
            tinta::pointF(points[i].x - originX, points[i].y - originY));
    }
    sink->EndFigure();
    return sink->Close();
}

static std::unique_ptr<tinta::PathGeometry> buildSliceGeometry(App& app, float radius,
                                                              float a0, float a1) {
    // Local space: circle center at (radius, radius)
    if (!app.renderTarget) return nullptr;
    std::unique_ptr<tinta::PathSink> sink = app.renderTarget->BeginPath();
    if (!sink) return nullptr;
    float cx = radius, cy = radius;
    tinta::PointF start =
        tinta::pointF(cx + radius * std::cos(a0), cy + radius * std::sin(a0));
    tinta::PointF end =
        tinta::pointF(cx + radius * std::cos(a1), cy + radius * std::sin(a1));
    float sweep = a1 - a0;
    if (sweep >= 6.28318f - 0.0001f) {
        // Full circle: two half arcs
        sink->BeginFigure(start);
        tinta::PointF opposite = tinta::pointF(
            cx + radius * std::cos(a0 + 3.14159f),
            cy + radius * std::sin(a0 + 3.14159f));
        // The three D2D1_ARC_SEGMENT fields tinta::ArcSegment does not carry were
        // {radius, radius}, 0.0f and CLOCKWISE at all four arc sites in the tree,
        // which is why the seam supplies them as constants. render_api.h has the
        // census; these two braced initialisers are two of the four it counted.
        sink->AddArc(tinta::ArcSegment{opposite, radius, /*largeArc=*/false});
        sink->AddArc(tinta::ArcSegment{start, radius, /*largeArc=*/false});
        sink->EndFigure();
    } else {
        sink->BeginFigure(tinta::pointF(cx, cy));
        sink->AddLine(start);
        sink->AddArc(tinta::ArcSegment{end, radius, sweep > 3.14159f});
        sink->EndFigure();
    }
    return sink->Close();
}

static bool layoutMermaidExtDiagram(App& app, mermaidext::Kind kind,
                                    const std::string& source,
                                    size_t sourceOffset, float& y,
                                    float indent, float maxWidth,
                                    tinta::RectF* renderedBounds,
                                    float scaleMul = 1.0f) {
    float scale = app.contentScale * app.zoomFactor * scaleMul;
    DiagramFormats formats(app);

    auto measureFn = [&](const std::string& text,
                         const mermaidext::TextStyle& style,
                         float wrapWidth) -> mermaidext::Size {
        if (text.empty()) return {};
        std::wstring wide = toWide(text);
        std::shared_ptr<tinta::TextLayout> layout = createDiagramTextLayout(
            app, formats, wide, style, scale,
            wrapWidth > 0.0f ? wrapWidth : kHugeWidth, kHugeWidth, -1, -1);
        if (!layout) return {};
        if (wrapWidth <= 0.0f) {
            layout->SetWordWrapping(tinta::WordWrapping::NoWrap);
        }
        tinta::TextMetrics metrics{};
        layout->GetMetrics(&metrics);
        return {metrics.widthIncludingTrailingWhitespace, metrics.height};
    };

    mermaidext::Built built =
        mermaidext::build(kind, source, measureFn, scale);
    if (!built.ok || built.prims.empty()) return false;

    float baseX = indent;
    if (built.width < maxWidth) {
        baseX += (maxWidth - built.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;

    if (sourceOffset != SIZE_MAX) {
        if (app.scrollAnchors.empty() ||
            app.scrollAnchors.back().sourceOffset < sourceOffset) {
            app.scrollAnchors.push_back({sourceOffset, baseY});
        }
    }

    // Text prims are appended to docText in reading order so selection
    // sweeps the diagram top-to-bottom
    std::vector<const mermaidext::Prim*> textPrims;

    for (const auto& prim : built.prims) {
        float x1 = baseX + prim.x1, y1 = baseY + prim.y1;
        float x2 = baseX + prim.x2, y2 = baseY + prim.y2;
        tinta::Color fill = resolveDiagramRoleImpl(app, prim, prim.fill);
        tinta::Color stroke = resolveDiagramRoleImpl(app, prim, prim.stroke);
        switch (prim.type) {
            case mermaidext::PrimType::Rect:
            case mermaidext::PrimType::RoundRect:
            case mermaidext::PrimType::Ellipse: {
                App::LayoutShape shape;
                shape.type =
                    prim.type == mermaidext::PrimType::Rect
                        ? App::LayoutShapeType::Rectangle
                    : prim.type == mermaidext::PrimType::RoundRect
                        ? App::LayoutShapeType::RoundedRectangle
                        : App::LayoutShapeType::Ellipse;
                shape.rect = tinta::rectF(x1, y1, x2, y2);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.radius = prim.radius;
                // std::move even though THIS shape holds no path: LayoutShape is
                // move-only now, so a copy here would not compile. Worth naming
                // because the diff looks like a gratuitous move on a plain struct.
                app.layoutShapes.push_back(std::move(shape));
                break;
            }
            case mermaidext::PrimType::Line: {
                App::LayoutConnector connector;
                connector.color = stroke;
                connector.stroke = prim.strokeWidth;
                connector.dashed = prim.dashed;
                connector.arrowSize = 8.0f * scale;
                connector.directed = prim.openArrow;
                float endX = x2, endY = y2;
                if (prim.arrow) {
                    // Filled triangle head: shorten the line, add a polygon
                    float dx = x2 - x1, dy = y2 - y1;
                    float length = std::sqrt(dx * dx + dy * dy);
                    if (length > 0.01f) {
                        dx /= length;
                        dy /= length;
                        float head = 9.0f * scale;
                        float wing = 4.5f * scale;
                        endX = x2 - dx * head * 0.7f;
                        endY = y2 - dy * head * 0.7f;
                        std::vector<mermaidext::Point> tri = {
                            {x2, y2},
                            {x2 - dx * head + dy * wing,
                             y2 - dy * head - dx * wing},
                            {x2 - dx * head - dy * wing,
                             y2 - dy * head + dx * wing},
                        };
                        float minX = std::min({tri[0].x, tri[1].x, tri[2].x});
                        float minY = std::min({tri[0].y, tri[1].y, tri[2].y});
                        float maxX = std::max({tri[0].x, tri[1].x, tri[2].x});
                        float maxY = std::max({tri[0].y, tri[1].y, tri[2].y});
                        App::LayoutShape triangle;
                        triangle.type = App::LayoutShapeType::Path;
                        triangle.rect = tinta::rectF(minX, minY, maxX, maxY);
                        triangle.fill = stroke;
                        triangle.stroke.a = 0.0f;
                        triangle.geometry =
                            buildPolygonGeometry(app, tri, minX, minY);
                        app.layoutShapes.push_back(std::move(triangle));
                    }
                }
                connector.points = {tinta::pointF(x1, y1),
                                    tinta::pointF(endX, endY)};
                connector.bounds = tinta::rectF(
                    std::min(x1, x2) - 10.0f * scale,
                    std::min(y1, y2) - 10.0f * scale,
                    std::max(x1, x2) + 10.0f * scale,
                    std::max(y1, y2) + 10.0f * scale);
                app.layoutConnectors.push_back(std::move(connector));
                break;
            }
            case mermaidext::PrimType::Polygon: {
                if (prim.pts.size() < 3) break;
                std::vector<mermaidext::Point> shifted = prim.pts;
                float minX = shifted[0].x, minY = shifted[0].y;
                float maxX = minX, maxY = minY;
                for (auto& point : shifted) {
                    point.x += baseX;
                    point.y += baseY;
                }
                minX = maxX = shifted[0].x;
                minY = maxY = shifted[0].y;
                for (const auto& point : shifted) {
                    minX = std::min(minX, point.x);
                    minY = std::min(minY, point.y);
                    maxX = std::max(maxX, point.x);
                    maxY = std::max(maxY, point.y);
                }
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = tinta::rectF(minX, minY, maxX, maxY);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry = buildPolygonGeometry(app, shifted, minX, minY);
                app.layoutShapes.push_back(std::move(shape));
                break;
            }
            case mermaidext::PrimType::Slice: {
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = tinta::rectF(
                    x1 - prim.radius, y1 - prim.radius,
                    x1 + prim.radius, y1 + prim.radius);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry =
                    buildSliceGeometry(app, prim.radius, prim.a0, prim.a1);
                app.layoutShapes.push_back(std::move(shape));
                break;
            }
            case mermaidext::PrimType::Text:
                textPrims.push_back(&prim);
                break;
        }
    }

    std::stable_sort(textPrims.begin(), textPrims.end(),
                     [](const mermaidext::Prim* a, const mermaidext::Prim* b) {
                         if (std::abs(a->y1 - b->y1) > kLineBucketTolerance) {
                             return a->y1 < b->y1;
                         }
                         return a->x1 < b->x1;
                     });
    for (const mermaidext::Prim* prim : textPrims) {
        std::wstring wide = toWide(prim->text);
        if (wide.empty()) continue;
        tinta::RectF rect = tinta::rectF(baseX + prim->x1, baseY + prim->y1,
                                       baseX + prim->x2, baseY + prim->y2);
        // Slight width slack: boxes are sized from measured text, and
        // re-wrapping at the exact measured width can push the last word
        // (or a CJK character) onto a phantom second line
        std::shared_ptr<tinta::TextLayout> layout = createDiagramTextLayout(
            app, formats, wide, prim->style, scale,
            rect.right - rect.left + 2.5f, rect.bottom - rect.top + 2.0f,
            prim->alignH, prim->alignV);
        if (!layout) continue;
        tinta::TextMetrics metrics{};
        layout->GetMetrics(&metrics);
        LayoutInfo info;
        info.layout = std::move(layout);
        info.width = metrics.widthIncludingTrailingWhitespace;
        info.height = metrics.height;
        size_t docStart = app.docText.size();
        app.docText += wide;
        addTextRun(app, std::move(info),
                   tinta::pointF(rect.left, rect.top), rect,
                   resolveDiagramRoleImpl(app, *prim, prim->fill),
                   docStart, wide.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    float diagramRight = baseX + built.width;
    float diagramBottom = baseY + built.height;
    app.contentWidth = std::max(app.contentWidth,
                                diagramRight + 40.0f * scale);
    if (renderedBounds) {
        *renderedBounds =
            tinta::rectF(baseX, baseY, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
        }
    }

    std::string languageName = elem->language;
    std::transform(
        languageName.begin(), languageName.end(), languageName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (languageName == "mermaid") {
        tinta::RectF renderedBounds{};
        mermaidext::Kind kind = mermaidext::detectKind(code);
        unsigned fitKey = 0x80000000u | app.layoutDiagramSeq++;
        bool fitActive = fitBlockEnabled(app, fitKey);
        bool fitCandidate = false;
        bool rendered = false;
        LayoutSnapshot preSnap = takeSnapshot(app);
        float preY = y;
        float scaleMul = 1.0f;
        // Label re-wrapping makes width nonlinear in scale, so fitting
        // shrinks iteratively from the measured bounds of each pass
        for (int pass = 0; pass < 4; pass++) {
            if (kind == mermaidext::Kind::Flowchart) {
                rendered = layoutMermaidDiagram(
                    app, code, elem->sourceOffset, y, indent, maxWidth,
                    &renderedBounds, scaleMul);
            } else if (kind != mermaidext::Kind::None) {
                rendered = layoutMermaidExtDiagram(
                    app, kind, code, elem->sourceOffset, y, indent, maxWidth,
                    &renderedBounds, scaleMul);
            }
            if (!rendered) break;
            float renderedW = renderedBounds.right - indent;
            if (renderedW > maxWidth + 1.0f) {
                if (pass == 0) fitCandidate = true;
                if (fitActive && scaleMul > 0.15f) {
                    scaleMul = std::max(
                        0.15f, scaleMul * (maxWidth / renderedW) * 0.98f);
                    rollbackTo(app, preSnap);
                    y = preY;
                    continue;
                }
            }
            break;
        }
        if (rendered) {
            App::CodeBlockInfo info;
            info.bounds = renderedBounds;
            info.codeText = toWide(code);
            info.isDiagram = true;
            info.fitKey = fitKey;
            info.fitCandidate = fitCandidate || fitActive;
            info.fitActive = fitActive;
            app.codeBlocks.push_back(std::move(info));
            return;
        }
        rollbackTo(app, preSnap);
        y = preY;
    }

    std::wstring langHint = toWide(elem->language);
    int language = detectLanguage(langHint);

    float scale = app.contentScale * app.zoomFactor;
    float lineHeight = 20.0f * scale;
    float padding = 12.0f * scale;

    // A terminal newline ends the last row; it does not start another.
    // Count earlier blank rows normally and keep the empty-block minimum.
    int lineCount = (code.empty() || code.back() != '\n') ? 1 : 0;
    for (char c : code) if (c == '\n') lineCount++;

    app.docText += L"\n";

    float blockHeight = lineCount * lineHeight + padding * 2;
    size_t bgRectIndex = app.layoutRects.size();
    // Peeking a plain-text file: the panel already frames the content,
    // so the whole-file wrapper block skips its own card
    bool frameless = app.peekFrameless;
    if (!frameless) {
        app.layoutRects.push_back({tinta::rectF(indent, y, indent + maxWidth, y + blockHeight),
                                   app.theme.codeBackground});
    }

    std::wstring wcode = toWide(code);

    // Track code block for copy button
    app.codeBlocks.push_back({
        tinta::rectF(indent, y, indent + maxWidth, y + blockHeight),
        wcode
    });
    float textY = y + padding;
    bool inBlockComment = false;
    size_t codeDocStart = app.docText.size();
    size_t lineStart = 0;
    float maxLineWidth = 0.0f;

    while (lineStart < wcode.length()) {
        size_t lineEnd = wcode.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = wcode.length();

        std::wstring wline = wcode.substr(lineStart, lineEnd - lineStart);
        if (!wline.empty() && wline.back() == L'\r') wline.pop_back();

        size_t lineDocStart = codeDocStart + lineStart;
        float lineWidth = 0.0f;
        size_t lineRun = (size_t)-1;

        if (language > 0) {
            std::vector<SyntaxToken> tokens = tokenizeLine(wline, language, inBlockComment);
            float tokenX = indent + padding;

            // Merge consecutive same-color tokens into one layout — a line
            // typically collapses to a handful of color runs instead of one
            // IDWriteTextLayout per token.
            size_t ti = 0;
            while (ti < tokens.size()) {
                if (tokens[ti].text.empty()) { ti++; continue; }

                tinta::Color runColor = getTokenColor(app.theme, tokens[ti].tokenType);
                std::wstring_view runText = tokens[ti].text;
                size_t tj = ti + 1;
                while (tj < tokens.size()) {
                    const auto& next = tokens[tj];
                    if (next.text.empty()) { tj++; continue; }
                    tinta::Color c = getTokenColor(app.theme, next.tokenType);
                    bool sameColor = c.r == runColor.r && c.g == runColor.g &&
                                     c.b == runColor.b && c.a == runColor.a;
                    // Tokens are views into wline; only merge physically
                    // adjacent ones so the combined view stays valid
                    bool adjacent = next.text.data() == runText.data() + runText.size();
                    if (!sameColor || !adjacent) break;
                    runText = std::wstring_view(runText.data(), runText.size() + next.text.size());
                    tj++;
                }

                LayoutInfo info = createLayout(app, runText, app.codeFormat, lineHeight, app.codeTypography);
                float runWidth = info.width;

                tinta::PointF pos = tinta::pointF(tokenX, textY);
                tinta::RectF bounds = tinta::rectF(tokenX, textY, tokenX + runWidth, textY + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, runColor,
                           lineDocStart, 0, false);

                tokenX += runWidth;
                lineWidth += runWidth;
                ti = tj;
            }
        } else {
            LayoutInfo info = createLayout(app, wline, app.codeFormat, lineHeight, app.codeTypography);
            lineWidth = info.width;
            tinta::PointF pos = tinta::pointF(indent + padding, textY);
            tinta::RectF bounds = tinta::rectF(indent + padding, textY,
                                             indent + padding + lineWidth, textY + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), pos, bounds, app.theme.code,
                       lineDocStart, wline.length(), false);
            if (app.layoutTextRuns.size() > runsBefore) {
                lineRun = app.layoutTextRuns.size() - 1;
            }
        }

        if (!wline.empty()) {
            tinta::RectF lineBounds = tinta::rectF(indent + padding, textY,
                indent + padding + lineWidth, textY + lineHeight);
            // Syntax-highlighted lines have no single covering layout;
            // monospace interpolation stays accurate there
            addTextRect(app, lineBounds, lineDocStart, wline.length(), lineRun);
        }

        maxLineWidth = std::max(maxLineWidth, lineWidth);
        textY += lineHeight;
        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
    }

    // Long code lines used to be clipped at the block edge with no way to
    // reach them — extend the block background and the document width so
    // wide code participates in horizontal scrolling like diagrams do
    float widestExtent = maxLineWidth + padding * 2;
    if (widestExtent > maxWidth) {
        if (!frameless) {
            app.layoutRects[bgRectIndex].rect.right = indent + widestExtent;
        }
        app.contentWidth = std::max(app.contentWidth, indent + widestExtent);
    }

    app.docText += wcode;
    app.docText += L"\n\n";
    y += blockHeight + 14 * scale;
}

static void layoutBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float quoteIndent = 20.0f * scale;
    float startY = y;
    const LayoutSnapshot contentStart = takeSnapshot(app);

    // GitHub alert callouts: accent-colored bar plus a bold title line, the icon here and
    // the label in the UI language (alertLabel). Colors are github.com's light/dark alert
    // accents, picked by theme.
    static const struct {
        const wchar_t* icon;
        uint32_t light;
        uint32_t dark;
    } ALERT_STYLES[] = {
        {L"\u24D8  ",            0x0969DA, 0x4493F8},  // circled info
        {L"\U0001F4A1\uFE0E  ",   0x1A7F37, 0x3FB950},  // bulb, text presentation
        {L"\u2757\uFE0E  ", 0x8250DF, 0xAB7DF8},  // exclamation
        {L"\u26A0\uFE0E  ",   0x9A6700, 0xD29922},  // warning triangle
        {L"\u26D4\uFE0E  ",   0xCF222E, 0xF85149},  // no-entry
    };

    tinta::Color barColor = app.theme.blockquoteBorder;
    if (elem->alertKind >= 1 && elem->alertKind <= 5) {
        const auto& style = ALERT_STYLES[elem->alertKind - 1];
        barColor = hexColor(app.theme.isDark ? style.dark : style.light);

        std::wstring title = std::wstring(style.icon) + alertLabel(app.currentLanguageIndex, elem->alertKind);
        LayoutInfo info = createLayout(app, title, app.textFormat, 24.0f, app.bodyTypography);
        if (info.layout) {
            // On the seam, and its old comment said why it was not: "native for the
            // same reason the ruby site above is: createLayout owns the typography
            // reach-through, so the layout it returns cannot be a tinta::TextLayout
            // until that is resolved." Resolved.
            tinta::TextRange range = {0, (uint32_t)title.length()};
            info.layout->SetFontWeight(tinta::FontWeight::SemiBold, range);
        }
        tinta::PointF pos = tinta::pointF(indent + quoteIndent, y);
        tinta::RectF bounds = tinta::rectF(indent + quoteIndent, y,
                                         indent + maxWidth, y + 24 * scale);
        addTextRun(app, std::move(info), pos, bounds, barColor, 0, 0, false);
        y += 30 * scale;
    }

    for (const auto& child : elem->children) {
        layoutElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }

    // Child layout advances y past its trailing margin. Keep that spacing
    // outside the bar, without moving the next block. Use retained content
    // bounds so nested quotes, code padding, tables and math stay covered.
    float contentBottom = startY;
    for (size_t i = contentStart.textRuns; i < app.layoutTextRuns.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutTextRuns[i].bounds.bottom);
    for (size_t i = contentStart.textRects; i < app.textRects.size(); ++i)
        contentBottom = std::max(contentBottom, app.textRects[i].rect.bottom);
    for (size_t i = contentStart.rects; i < app.layoutRects.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutRects[i].rect.bottom);
    for (size_t i = contentStart.lines; i < app.layoutLines.size(); ++i) {
        const auto& line = app.layoutLines[i];
        contentBottom = std::max(contentBottom, std::max(line.p1.y, line.p2.y) + line.stroke / 2);
    }
    for (size_t i = contentStart.shapes; i < app.layoutShapes.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutShapes[i].rect.bottom);
    for (size_t i = contentStart.connectors; i < app.layoutConnectors.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutConnectors[i].bounds.bottom);
    for (size_t i = contentStart.bitmaps; i < app.layoutBitmaps.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutBitmaps[i].destRect.bottom);

    app.layoutRects.push_back({tinta::rectF(indent, startY, indent + 4, std::min(y, contentBottom)), barColor});
}

static void layoutList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float listIndent = 24.0f * scale;
    int itemNum = elem->start;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;

        if (child->isTask) {
            // Task item: a clickable checkbox replaces the bullet
            float box = 15.0f * scale;
            float boxX = indent + 1.0f * scale;
            float boxY = y + 4.0f * scale;
            tinta::RectF boxRect = tinta::rectF(boxX, boxY, boxX + box, boxY + box);
            if (child->taskChecked) {
                app.layoutRects.push_back({boxRect, app.theme.accent});
                tinta::Color mark = app.theme.isDark
                    ? tinta::colorF(0.08f, 0.08f, 0.10f) : tinta::colorF(1, 1, 1);
                app.layoutLines.push_back({
                    tinta::pointF(boxX + box * 0.24f, boxY + box * 0.52f),
                    tinta::pointF(boxX + box * 0.44f, boxY + box * 0.74f),
                    mark, 2.0f * scale});
                app.layoutLines.push_back({
                    tinta::pointF(boxX + box * 0.44f, boxY + box * 0.74f),
                    tinta::pointF(boxX + box * 0.78f, boxY + box * 0.28f),
                    mark, 2.0f * scale});
            } else {
                tinta::Color edge = app.theme.blockquoteBorder;
                float w = 1.2f * scale;
                app.layoutLines.push_back({{boxX, boxY}, {boxX + box, boxY}, edge, w});
                app.layoutLines.push_back({{boxX, boxY + box}, {boxX + box, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX, boxY}, {boxX, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX + box, boxY}, {boxX + box, boxY + box}, edge, w});
            }
            if (child->taskMarkOffset != SIZE_MAX) {
                app.taskRects.push_back({
                    tinta::rectF(boxX - 4 * scale, boxY - 4 * scale,
                                boxX + box + 4 * scale, boxY + box + 4 * scale),
                    child->taskMarkOffset, child->taskChecked});
            }
        } else {
            std::wstring marker = elem->ordered ?
                std::to_wstring(itemNum++) + L"." : L"\x2022";

            LayoutInfo info = createLayout(app, marker, app.textFormat, 24.0f, app.bodyTypography);
            tinta::PointF pos = tinta::pointF(indent, y);
            tinta::RectF bounds = tinta::rectF(indent, y, indent + listIndent, y + 24);
            addTextRun(app, std::move(info), pos, bounds, app.theme.text, 0, 0, false);
        }

        bool hasBlockChildren = false;
        for (const auto& itemChild : child->children) {
            if (itemChild->type == ElementType::Paragraph ||
                itemChild->type == ElementType::List ||
                itemChild->type == ElementType::CodeBlock ||
                itemChild->type == ElementType::BlockQuote) {
                hasBlockChildren = true;
                break;
            }
        }

        float itemStartY = y;
        if (hasBlockChildren) {
            std::vector<ElementPtr> inlineElements, blockElements;
            for (const auto& itemChild : child->children) {
                if (itemChild->type == ElementType::Paragraph ||
                    itemChild->type == ElementType::List ||
                    itemChild->type == ElementType::CodeBlock ||
                    itemChild->type == ElementType::BlockQuote) {
                    blockElements.push_back(itemChild);
                } else {
                    inlineElements.push_back(itemChild);
                }
            }

            if (!inlineElements.empty()) {
                layoutInlineContent(app, inlineElements, indent + listIndent, y,
                    maxWidth - listIndent, app.textFormat, app.theme.text);
            }

            for (const auto& blockChild : blockElements) {
                layoutElement(app, blockChild, y, indent + listIndent, maxWidth - listIndent);
            }
        } else {
            layoutInlineContent(app, child->children, indent + listIndent, y,
                maxWidth - listIndent, app.textFormat, app.theme.text);
        }

        app.docText += L"\n\n";

        if (y < itemStartY + 28 * scale) {
            y = itemStartY + 28 * scale;
        }
    }
    y += 8 * scale;
}

// Result of a background image download+decode, handed to the UI thread
// via WM_APP_IMAGE_READY. Pixels are premultiplied BGRA.
struct AsyncImageResult {
    std::string src;
    // `unsigned`, not UINT, and not uint32_t either. UINT was a Windows spelling
    // for a value that never comes from Windows: both fields are assigned from
    // tinta::DecodedImage, which the image seam declares as plain `unsigned`, so
    // this now matches its only source exactly rather than matching it by
    // coincidence. Nine of this file's errors off Windows were these two lines --
    // seven of them cascade, the compiler having given up on the struct.
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint8_t> pixels;
    bool ok = false;
};

// Runs on a worker thread: blocking download + WIC decode never touch the
// UI thread, so dead links can't stall layout (#44). The worker owns its own
// COM apartment and WIC factory; only plain pixel bytes cross the thread
// boundary.
static void asyncImageWorker(std::string src) {
    auto* result = new AsyncImageResult();
    result->src = src;

    // Download and decode are two seam calls rather than one, because the decode half
    // is assertable in a test and the fetch half is not -- see image_api.h. The 16384
    // dimension bound and the download-to-cache-file mechanism both moved with the
    // code and are unchanged; the COM apartment this function used to open around its
    // whole body now lives in the Windows backend, which is the only half that needs
    // one.
    const std::wstring tempPath = tinta::fetchToTempFile(src);
    if (!tempPath.empty()) {
        tinta::DecodedImage decoded = tinta::decodeImageFile(tempPath);
        if (decoded.ok) {
            result->width = decoded.width;
            result->height = decoded.height;
            result->pixels = std::move(decoded.pixels);
            result->ok = true;
        }
    }

    if (!postWindowMessage(tinta::PostedMessage::ImageReady, 0,
                           reinterpret_cast<std::intptr_t>(result))) {
        delete result;  // window already gone
    }
}

// SVG rasterizes at 2x through the image seam -- Direct2D's SVG document renderer on
// Windows, QSvgRenderer on Qt. Both were 128 lines of device-context and memory-stream
// plumbing in this file, and both are now one call.
//
// The 192 dpi is load-bearing. entry.width/height carry the document's INTRINSIC size
// in DIPs while the bitmap holds 2x that many pixels, and declaring the bitmap at
// twice the default 96 dpi is what keeps those consistent: the backend then reports
// the bitmap's logical size as the intrinsic size, so every draw and hit-test
// downstream sees one coherent number. Passing 96 would draw every SVG in the document
// at double size, and nothing else would look wrong.
//
// That used to read "the reason this function still exists at all rather than folding
// into the seam", on the grounds that bitmap properties were Direct2D-only.
// RenderTarget::CreateBitmap now takes the dpi, so the argument is gone and the
// function survives for a smaller reason: rasterize, then declare, then record two
// different sizes on one entry.
static const float kSvgRasterScale = 2.0f;

static bool loadSvgBitmap(App& app, const std::wstring& path,
                          App::ImageEntry& entry) {
    // The render target only. This also refused when App's Direct2D or WIC factory was
    // null, which on qinta it always was -- so until 2026-10-07 no SVG image ever loaded
    // there, and every one drew as its alt-text placeholder. The seam owns its own
    // rasteriser; the factories left with the Direct2D stack, and
    // tests/fixtures/svg-image-mixed.md is the document that showed it.
    if (!app.renderTarget) return false;

    float svgW = 0.0f, svgH = 0.0f;
    tinta::DecodedImage raster =
        tinta::rasterizeSvgFile(path, kSvgRasterScale, svgW, svgH);
    if (!raster.ok) return false;

    // 96 * kSvgRasterScale, i.e. 192 -- see the comment above kSvgRasterScale for why
    // this argument is the one that must not be defaulted.
    std::shared_ptr<tinta::Bitmap> bitmap = app.renderTarget->CreateBitmap(
        raster.width, raster.height, raster.pixels.data(), raster.width * 4,
        96.0f * kSvgRasterScale);
    if (bitmap) {
        entry.bitmap = std::move(bitmap);
        entry.width = (int)svgW;
        entry.height = (int)svgH;
        entry.failed = false;
    }
    return !entry.failed;
}

static App::ImageEntry& getOrLoadImage(App& app, const std::string& src) {
    auto it = app.imageCache.find(src);
    if (it != app.imageCache.end()) {
        app.touchImageCacheEntry(it->second);
        return it->second;
    }

    App::ImageEntry entry;
    entry.failed = true;  // assume failure

    // WAS `!app.wicFactory || !app.renderTarget`, and dropping the first half is a
    // behaviour change rather than a tidy-up, so it is argued rather than done quietly.
    //
    // It HAD to go: app.wicFactory is a Windows-only COM pointer that is permanently
    // null on Qt, so this test would have failed every image in the Qt build while
    // compiling perfectly -- exactly the class of silent breakage a seam is supposed to
    // prevent. The null test itself compiles off Windows (app.h forward-declares the COM
    // types), which is why the census never flagged it and why it needed finding by hand.
    //
    // What is lost on Windows, stated precisely: this used to refuse the decode when the
    // App's process-lifetime WIC factory was missing. It no longer looks, because the
    // decode no longer uses that factory -- decodeImageFileToBitmap creates its own. So
    // the case this half covered now fails one level down, inside the backend, and
    // returns nullptr, which lands on the same `entry.failed = true` the caller already
    // set. Same outcome by a different route, and the surviving test names the one thing
    // this function actually still needs.
    if (!app.renderTarget) {
        app.storeImageCacheEntry(src, std::move(entry));
        return app.imageCache[src];
    }

    std::wstring widePath;

    // Remote images load on a worker thread — a synchronous download here
    // stalled layout for seconds per unreachable URL (#44)
    bool isUrl = (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0);
    if (isUrl) {
        entry.failed = false;
        entry.pending = true;
        app.storeImageCacheEntry(src, std::move(entry));
        std::thread(asyncImageWorker, src).detach();
        return app.imageCache[src];
    } else {
        // Resolve relative to the current file's directory. Both strings are
        // UTF-8 and must be widened explicitly: a narrow filesystem::path
        // decodes through the ANSI codepage and mangles non-ASCII paths (#87)
        std::wstring wsrc = toWide(src);
        if (!app.currentFile.empty()) {
            std::filesystem::path basePath(toWide(app.currentFile));
            std::filesystem::path imgPath = basePath.parent_path() / wsrc;
            widePath = imgPath.wstring();
        } else {
            widePath = wsrc;
        }
    }

    // SVG renders through Direct2D's SVG document support instead of WIC
    {
        std::wstring lower = widePath;
        for (auto& c : lower) c = (wchar_t)towlower(c);
        if (lower.size() > 4 &&
            lower.compare(lower.size() - 4, 4, L".svg") == 0) {
            loadSvgBitmap(app, widePath, entry);
            app.storeImageCacheEntry(src, std::move(entry));
            return app.imageCache[src];
        }
    }

    // Decode straight into a backend bitmap. The 56 lines of WIC that used to sit here
    // -- decoder, frame, format converter, CreateBitmapFromWicBitmap, and the four
    // release-and-bail ladders between them -- are now tinta::decodeImageFileToBitmap,
    // implemented once per backend. The sequence moved verbatim rather than being
    // rewritten, and the previous comment on this branch predicted the shape of this
    // change exactly: "when this branch is ported, this is the line that changes and
    // nothing downstream of it does." It was one line out. Nothing downstream changed.
    //
    // Still NOT the seam's CreateBitmap, and for the reason image_api.h now argues at
    // length: going through tinta::DecodedImage would add a full-image copy to every
    // local image on Direct2D and two on Qt. A separate entry point is what avoids it.
    if (auto bitmap = tinta::decodeImageFileToBitmap(*app.renderTarget, widePath)) {
        tinta::SizeF size = bitmap->GetSize();
        entry.bitmap = std::move(bitmap);
        entry.width = (int)size.width;
        entry.height = (int)size.height;
        entry.failed = false;
    }

    app.storeImageCacheEntry(src, std::move(entry));
    return app.imageCache[src];
}

// The size a loaded image draws at within maxWidth; false while it is pending or failed
static bool imageDisplaySize(App& app, const ElementPtr& elem, float maxWidth,
                             float& displayW, float& displayH) {
    auto& entry = getOrLoadImage(app, elem->url);
    if (entry.failed || !entry.bitmap) return false;

    float scale = app.contentScale * app.zoomFactor;
    float imgW = (float)entry.width;
    float imgH = (float)entry.height;
    // HTML width=/height= in CSS px; with one of the two, the other follows the aspect.
    // This may upscale, as a browser would, unlike an image's own size below.
    if (imgW > 0 && imgH > 0 && (elem->width > 0 || elem->height > 0)) {
        float w = elem->width > 0 ? elem->width : elem->height * imgW / imgH;
        float h = elem->height > 0 ? elem->height : elem->width * imgH / imgW;
        imgW = w * scale;
        imgH = h * scale;
    }

    // Scale to fit within maxWidth, never upscale
    float displayScale = std::min(1.0f, maxWidth / imgW);
    displayW = imgW * displayScale;
    displayH = imgH * displayScale;

    // Cap max height
    float maxH = 600.0f * scale;
    if (displayH > maxH) {
        displayH = maxH;
        displayW = displayH * (imgW / imgH);
    }
    return true;
}

static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float displayW = 0, displayH = 0;
    if (!imageDisplaySize(app, elem, maxWidth, displayW, displayH)) {
        // Render alt text as placeholder
        std::wstring altText = L"[image";
        std::wstring alt;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) alt += toWide(e->text);
            else for (const auto& c : e->children) extract(c);
        };
        for (const auto& c : elem->children) extract(c);
        if (!alt.empty()) {
            altText += L": " + alt;
        }
        altText += L"]";

        float lineHeight = app.italicFormat->GetFontSize() * 1.7f;
        size_t docStart = app.docText.size();
        LayoutInfo info = createLayout(app, altText, app.italicFormat, lineHeight, app.bodyTypography);

        tinta::Color color = app.theme.text;
        color.a = 0.6f;
        tinta::PointF pos = tinta::pointF(indent, y);
        tinta::RectF bounds = tinta::rectF(indent, y, indent + info.width, y + lineHeight);
        addTextRun(app, std::move(info), pos, bounds, color, docStart, altText.length(), false);
        app.docText += altText;
        y += lineHeight;
        return;
    }

    float scale = app.contentScale * app.zoomFactor;
    app.layoutBitmaps.push_back({getOrLoadImage(app, elem->url).bitmap,
        tinta::rectF(indent, y, indent + displayW, y + displayH)});

    y += displayH + 12 * scale;
}

static void layoutTable(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float cellPadding = 8.0f * scale;
    float fontSize = app.textFormat->GetFontSize();
    float lineHeight = fontSize * 1.7f;
    float minColWidth = 40.0f * scale;

    // Collect rows
    std::vector<Element*> rows;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::TableRow) {
            rows.push_back(child.get());
        }
    }
    if (rows.empty()) return;

    // Determine column count
    int colCount = elem->col_count;
    if (colCount <= 0 && !rows.empty()) {
        colCount = (int)rows[0]->children.size();
    }
    if (colCount <= 0) return;

    // Pass 1: Measure natural widths using plain text extraction (cheap, approximate)
    std::vector<float> colWidths(colCount, minColWidth);
    std::vector<float> rowHeights(rows.size(), lineHeight + cellPadding * 2);
    std::vector<std::vector<int>> cellAligns(rows.size(), std::vector<int>(colCount, 0));
    // Per-cell natural width + plain-text flag: a simple cell that fits its
    // final column is exactly one line tall, so the height-measuring trial
    // layout in pass 1b can be skipped for it entirely
    std::vector<std::vector<float>> cellNatural(rows.size(), std::vector<float>(colCount, 0.0f));
    std::vector<std::vector<uint8_t>> cellSimple(rows.size(), std::vector<uint8_t>(colCount, 0));

    std::wstring tsv;  // per-cell plain text doubles as the copy payload
    for (size_t r = 0; r < rows.size(); r++) {
        const auto& row = rows[r];
        if (r > 0) tsv += L"\n";
        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            cellAligns[r][c] = cell->align;

            // Extract plain text for width estimation
            std::wstring text;
            std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
                if (!e) return;
                if (e->type == ElementType::Text) text += toWide(e->text);
                else for (const auto& ch : e->children) extract(ch);
            };
            for (const auto& ch : cell->children) extract(ch);

            if (c > 0) tsv += L"\t";
            std::wstring tsvCell = text;
            for (wchar_t& ch : tsvCell) {
                if (ch == L'\t' || ch == L'\n' || ch == L'\r') ch = L' ';
            }
            tsv += tsvCell;

            // Measure natural width THE WAY THE RENDERER SHAPES IT — same
            // typography and the language-aware font fallback. A raw
            // layout here picked a different CJK font than the renderer,
            // whose slightly wider advances then wrapped a "fitting" cell
            // one line past its measured row height (#148)
            bool isHeader = (r == 0);
            tinta::TextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
            bool plainCell = cell->children.size() == 1 &&
                             cell->children[0]->type == ElementType::Text;
            float textWidth = 0;
            if (plainCell && !text.empty() && fmt) {
                LayoutInfo mi = createLayout(app, text, fmt, lineHeight,
                                             app.bodyTypography);
                textWidth = mi.width;  // measurement only; `mi` dies with the scope
            } else if (!cell->children.empty() && fmt) {
                // Styled cell: plain-text extraction lies about width —
                // inline code renders in the mono face plus a padded chip
                // and never wraps internally, so an under-measured column
                // pushed chips past the table border. Lay the cell out at
                // unlimited width with the real renderer and take its
                // right edge (chip rects included), then roll back.
                LayoutSnapshot snap = takeSnapshot(app);
                float cy = 0.0f;
                layoutInlineContent(app, cell->children, 0.0f, cy,
                                    kHugeWidth, fmt, app.theme.text, {},
                                    lineHeight);
                float maxRight = 0.0f;
                for (size_t i = snap.textRuns; i < app.layoutTextRuns.size();
                     i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutTextRuns[i].bounds.right);
                }
                for (size_t i = snap.rects; i < app.layoutRects.size(); i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutRects[i].rect.right);
                }
                for (size_t i = snap.bitmaps; i < app.layoutBitmaps.size(); i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutBitmaps[i].destRect.right);
                }
                rollbackTo(app, snap);
                textWidth = maxRight;
            }
            float needed = textWidth + cellPadding * 2 + 6.0f * scale;
            if (needed > colWidths[c]) colWidths[c] = needed;
            cellNatural[r][c] = needed;
            cellSimple[r][c] = cell->children.size() == 1 &&
                               cell->children[0]->type == ElementType::Text;
        }
    }

    // Distribute widths like a browser's auto table layout: columns whose
    // natural width is under their fair share keep it untouched; only the
    // wide columns shrink, splitting the remaining space proportionally.
    // Pure proportional scaling starved narrow columns (a 3-character CJK
    // header ended up one character wide) while a single huge cell hogged
    // the row (#24).
    float totalWidth = 0;
    for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];

    unsigned fitKey = 0x40000000u | app.layoutTableSeq++;
    bool fitCandidate = false;
    bool fitActive = fitBlockEnabled(app, fitKey);

    if (totalWidth > maxWidth && fitActive) {
        // Fit-to-width override: shrink every column proportionally with
        // no minimum floor; dense cells wrap hard, nothing scrolls
        float k = maxWidth / totalWidth;
        for (int c = 0; c < colCount; c++) colWidths[c] *= k;
        totalWidth = maxWidth;
        fitCandidate = true;
    } else if (totalWidth > maxWidth) {
        // Floor: at least ~2.5 CJK glyphs per line so no column degenerates
        // into a vertical strip
        float minCol = std::max(minColWidth, fontSize * 2.5f + cellPadding * 2);
        std::vector<bool> fixed(colCount, false);
        float available = maxWidth;
        int unfixedCount = colCount;
        bool changed = true;
        while (changed && unfixedCount > 0) {
            changed = false;
            float fair = available / unfixedCount;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c] && colWidths[c] <= fair) {
                    fixed[c] = true;
                    available -= colWidths[c];
                    unfixedCount--;
                    changed = true;
                }
            }
        }
        if (unfixedCount > 0) {
            float naturalSum = 0;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) naturalSum += colWidths[c];
            }
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) {
                    float w = (naturalSum > 0.0f)
                        ? available * (colWidths[c] / naturalSum)
                        : available / unfixedCount;
                    colWidths[c] = std::max(minCol, w);
                }
            }
        }
        totalWidth = 0;
        for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];
        // Minimum widths can push past maxWidth; the table then joins
        // horizontal scrolling instead of squeezing columns unreadably
        // (unless the fit toggle squeezes it on purpose)
        if (totalWidth > maxWidth + 1.0f) fitCandidate = true;
        app.contentWidth = std::max(app.contentWidth, indent + totalWidth);
    }

    // Pass 1b: Measure row heights via layoutInlineContent with snapshot-rollback
    for (size_t r = 0; r < rows.size(); r++) {
        float maxRowH = lineHeight + cellPadding * 2;
        bool isHeader = (r == 0);
        tinta::TextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
        tinta::Color textColor = isHeader ? app.theme.heading : app.theme.text;
        const auto& row = rows[r];

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            if (cell->children.empty()) continue;

            // A plain-text cell whose natural width fits the column is one
            // line tall — no trial layout needed (the common case)
            if (cellSimple[r][c] && cellNatural[r][c] <= colWidths[c]) continue;

            float cellW = colWidths[c] - cellPadding * 2;
            LayoutSnapshot snap = takeSnapshot(app);
            float cellY = 0.0f;
            layoutInlineContent(app, cell->children, 0.0f, cellY, cellW,
                                fmt, textColor, {}, lineHeight);
            rollbackTo(app, snap);

            float h = cellY + cellPadding * 2;
            if (h > maxRowH) maxRowH = h;
        }
        rowHeights[r] = maxRowH;
    }

    // The Table element itself never receives a sourceOffset (the parser
    // stamps the deepest element at text time), so the table's source
    // anchor is its first text-bearing descendant - the same convention
    // the in-place cell editor's source parser assumes (#148)
    size_t tableSrc = SIZE_MAX;
    {
        std::function<void(const ElementPtr&)> firstSrc =
            [&](const ElementPtr& e) {
                if (!e || tableSrc != SIZE_MAX) return;
                if (e->sourceOffset != SIZE_MAX) {
                    tableSrc = e->sourceOffset;
                    return;
                }
                for (const auto& child : e->children) firstSrc(child);
            };
        for (const auto& child : elem->children) {
            firstSrc(child);
            if (tableSrc != SIZE_MAX) break;
        }
    }

    // Pass 2: Render cells via layoutInlineContent (produces real text runs, links, etc.)
    float tableStartY = y;
    tinta::Color borderColor = app.theme.blockquoteBorder;
    float borderStroke = 1.0f * scale;

    for (size_t r = 0; r < rows.size(); r++) {
        float cellX = indent;
        bool isHeader = (r == 0);
        const auto& row = rows[r];

        // Header row background
        if (isHeader) {
            tinta::Color headerBg = app.theme.codeBackground;
            headerBg.a = 0.5f;
            app.layoutRects.push_back({tinta::rectF(indent, y, indent + totalWidth, y + rowHeights[r]), headerBg});
        } else if (r % 2 == 0) {
            // Subtle alternating row background
            tinta::Color altBg = app.theme.codeBackground;
            altBg.a = 0.15f;
            app.layoutRects.push_back({tinta::rectF(indent, y, indent + totalWidth, y + rowHeights[r]), altBg});
        }

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            tinta::TextFormat* fmt = isHeader ? app.boldFormat : app.textFormat;
            tinta::Color textColor = isHeader ? app.theme.heading : app.theme.text;

            // In-place editing (#148): the preview records every cell's
            // rect keyed by the table's source anchor plus row/col
            if (app.editMode && tableSrc != SIZE_MAX) {
                app.tableCellRects.push_back(
                    {tableSrc, (int)r, (int)c,
                     tinta::rectF(cellX, y, cellX + colWidths[c],
                                 y + rowHeights[r])});
            }

            if (!cell->children.empty()) {
                float cellW = colWidths[c] - cellPadding * 2;
                float textX = cellX + cellPadding;
                float textY = y + cellPadding;

                int align = cellAligns[r][c];
                LayoutSnapshot cellSnap = takeSnapshot(app);

                layoutInlineContent(app, cell->children, textX, textY, cellW,
                                    fmt, textColor, {}, lineHeight);

                // Apply center/right alignment by shifting all new items
                if (align == 2 || align == 3) {
                    float maxRight = 0.0f;
                    for (size_t i = cellSnap.textRuns; i < app.layoutTextRuns.size(); i++) {
                        maxRight = std::max(maxRight, app.layoutTextRuns[i].bounds.right);
                    }
                    for (size_t i = cellSnap.bitmaps; i < app.layoutBitmaps.size(); i++) {
                        maxRight = std::max(maxRight, app.layoutBitmaps[i].destRect.right);
                    }
                    float contentW = maxRight - textX;
                    float dx = 0.0f;
                    if (align == 2) { // center
                        dx = (cellW - contentW) / 2.0f;
                    } else { // right
                        dx = cellW - contentW;
                    }
                    if (dx > 0.0f) {
                        shiftLayoutItems(app, cellSnap, dx);
                    }
                }
            }

            cellX += colWidths[c];
        }
        app.docText += L"\n";
        y += rowHeights[r];
    }

    // Grid lines: horizontal
    for (size_t r = 0; r <= rows.size(); r++) {
        float lineY = tableStartY;
        for (size_t i = 0; i < r; i++) lineY += rowHeights[i];
        float stroke = (r == 1) ? borderStroke * 2 : borderStroke; // thicker after header
        app.layoutLines.push_back({tinta::pointF(indent, lineY),
                                   tinta::pointF(indent + totalWidth, lineY),
                                   borderColor, stroke});
    }

    // Grid lines: vertical
    {
        float tableEndY = tableStartY;
        for (size_t r = 0; r < rows.size(); r++) tableEndY += rowHeights[r];
        float vx = indent;
        for (int c = 0; c <= colCount; c++) {
            app.layoutLines.push_back({tinta::pointF(vx, tableStartY),
                                       tinta::pointF(vx, tableEndY),
                                       borderColor, borderStroke});
            if (c < colCount) vx += colWidths[c];
        }

        // Copy-as-TSV hover button hit region
        App::TableInfo info;
        info.bounds =
            tinta::rectF(indent, tableStartY, indent + totalWidth, tableEndY);
        info.tsv = std::move(tsv);
        info.fitKey = fitKey;
        info.fitCandidate = fitCandidate || fitActive;
        info.fitActive = fitActive;
        info.sourceOffset = tableSrc;
        app.tableRects.push_back(std::move(info));
    }

    app.docText += L"\n";
    y += 14 * scale;
}

static void layoutHorizontalRule(App& app, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    y += 16 * scale;
    app.layoutLines.push_back({tinta::pointF(indent, y),
                               tinta::pointF(indent + maxWidth, y),
                               app.theme.blockquoteBorder, scale});
    y += 16 * scale;
}

// Frontmatter properties strip: the title as muted text and the tags as
// small #chips, one row above the document (wrapping when needed)
static void layoutProperties(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    tinta::TextFormat* chipFormat = app.supSubFormat ? app.supSubFormat : app.textFormat;
    float chipH = 22.0f * scale;
    float padX = 8.0f * scale;
    float gap = 6.0f * scale;
    float x = indent;

    auto chip = [&](const std::wstring& text, tinta::Color textColor, bool pill) {
        LayoutInfo info = createLayout(app, text, chipFormat, chipH, app.bodyTypography);
        float w = info.width;
        if (x + w + padX * 2 > indent + maxWidth && x > indent) {
            x = indent;
            y += chipH + gap;
        }
        if (pill) {
            tinta::Color bgc = app.theme.codeBackground;
            app.layoutRects.push_back({tinta::rectF(x, y, x + w + padX * 2, y + chipH), bgc});
        }
        float textY = y + (chipH - info.height) / 2;
        tinta::PointF pos = tinta::pointF(x + (pill ? padX : 0), textY);
        tinta::RectF bounds = tinta::rectF(pos.x, y, pos.x + w, y + chipH);
        addTextRun(app, std::move(info), pos, bounds, textColor, 0, 0, false);
        x += w + (pill ? padX * 2 : 0) + gap;
    };

    if (!elem->text.empty()) {
        tinta::Color titleColor = app.theme.text;
        titleColor.a = 0.65f;
        chip(toWide(elem->text), titleColor, false);
    }
    for (const auto& tag : elem->children) {
        if (tag->type != ElementType::Text || tag->text.empty()) continue;
        chip(L"#" + toWide(tag->text), app.theme.accent, true);
    }
    y += chipH + 14.0f * scale;
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Properties:
            layoutProperties(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Paragraph:
            layoutParagraph(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Heading:
            layoutHeading(app, elem, y, indent, maxWidth);
            break;
        case ElementType::CodeBlock:
            layoutCodeBlock(app, elem, y, indent, maxWidth);
            break;
        case ElementType::MermaidDiagram:
            if (!layoutMermaidDiagram(
                    app, elem->text, elem->sourceOffset, y, indent, maxWidth)) {
                app.focusMermaidOnNextLayout = false;
                auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
                auto text = std::make_shared<Element>(ElementType::Text);
                text->text = elem->text;
                text->parent = fallback.get();
                fallback->children.push_back(std::move(text));
                fallback->sourceOffset = elem->sourceOffset;
                layoutCodeBlock(app, fallback, y, indent, maxWidth);
            }
            break;
        case ElementType::BlockQuote:
            layoutBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            layoutList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            layoutHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::Table:
            layoutTable(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock: {
            // HtmlBlock can contain both block elements (Paragraph, List, etc.)
            // and inline elements (Text, Ruby, Link, etc.). Collect consecutive
            // inline children and render them through layoutInlineContent.
            std::vector<ElementPtr> inlineBuffer;
            auto flushInline = [&]() {
                if (!inlineBuffer.empty()) {
                    layoutInlineContent(app, inlineBuffer, indent, y, maxWidth,
                                        app.textFormat, app.theme.text);
                    app.docText += L"\n\n";
                    // Bare <br>s between blocks (a README's spacer) are blank lines,
                    // not a paragraph, so they take no paragraph gap
                    bool onlyBreaks = std::all_of(inlineBuffer.begin(), inlineBuffer.end(),
                        [](const ElementPtr& e) { return e->type == ElementType::HardBreak; });
                    float s = app.contentScale * app.zoomFactor;
                    if (!onlyBreaks) y += 14 * s;
                    inlineBuffer.clear();
                }
            };
            for (const auto& child : elem->children) {
                bool isBlock = (child->type == ElementType::Paragraph ||
                                child->type == ElementType::Heading ||
                                child->type == ElementType::CodeBlock ||
                                child->type == ElementType::BlockQuote ||
                                child->type == ElementType::List ||
                                child->type == ElementType::HorizontalRule ||
                                child->type == ElementType::HtmlBlock ||
                                child->type == ElementType::Table);
                if (isBlock) {
                    flushInline();
                    layoutElement(app, child, y, indent, maxWidth);
                } else {
                    inlineBuffer.push_back(child);
                }
            }
            flushInline();
            break;
        }
        default:
            for (const auto& child : elem->children) {
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}

// Find first valid sourceOffset in an element subtree
static size_t findFirstSourceOffset(const ElementPtr& elem) {
    if (!elem) return SIZE_MAX;
    if (elem->sourceOffset != SIZE_MAX) return elem->sourceOffset;
    for (const auto& child : elem->children) {
        size_t off = findFirstSourceOffset(child);
        if (off != SIZE_MAX) return off;
    }
    return SIZE_MAX;
}

// Count total elements in AST for vector pre-allocation
static size_t countElements(const ElementPtr& elem) {
    if (!elem) return 0;
    size_t count = 1;
    for (const auto& child : elem->children) {
        count += countElements(child);
    }
    return count;
}

} // namespace

namespace {

// Reset layout state and prepare for laying out blocks. Returns false when
// there is nothing to lay out (no document).
bool layoutBegin(App& app) {
    app.clearLayoutCache();
    app.layoutTimeUs = 0;

    if (!app.root) {
        app.contentHeight = 0;
        app.contentWidth = app.width;
        app.layoutComplete = true;
        return false;
    }

    // Pre-allocate vectors based on estimated element count
    size_t elemCount = countElements(app.root);
    app.layoutTextRuns.reserve(elemCount * 2);
    app.layoutRects.reserve(elemCount);
    app.layoutLines.reserve(elemCount);
    app.layoutShapes.reserve(elemCount);
    app.layoutConnectors.reserve(elemCount);
    app.linkRects.reserve(elemCount / 4);
    app.textRects.reserve(elemCount * 2);
    app.lineBuckets.reserve(elemCount);
    app.docText.reserve(elemCount * 20);  // ~20 chars per element average

    float scale = app.contentScale * app.zoomFactor;

    float layoutWidth = documentViewportWidth(app);

    app.layoutIndent = 40.0f * scale;
    app.layoutMaxWidth = layoutWidth - app.layoutIndent * 2;
    // Reading column (#82): a centered percentage of the window, with a
    // separate preference for fullscreen (zen). Edit-mode panes are exempt.
    {
        int pct = app.zenMode ? app.zenWidthPct : app.readingWidthPct;
        if (pct < 100 && !app.editMode) {
            float column = app.layoutMaxWidth * (float)pct / 100.0f;
            app.layoutIndent += (app.layoutMaxWidth - column) / 2.0f;
            app.layoutMaxWidth = column;
        }
    }
    // Content starts below the title-bar tab strip; the strip paints over
    // anything that scrolls up under it. On the edit-mode floating sheet
    // (design 10a) content instead starts just inside the sheet's top
    // edge — the sheet rises past the strip, so the page top is its own.
    app.layoutCursorY = editorPreviewVisible(app)
                            ? editSheetRect(app).top + dpi(app, 18.0f)
                            : chromeTopHeight(app) + 20.0f * scale;
    app.layoutNextBlock = 0;
    app.layoutComplete = false;
    app.contentWidth = layoutWidth;
    app.scrollAnchors.clear();
    app.layoutTableSeq = 0;
    app.layoutDiagramSeq = 0;
    return true;
}

// Lay out top-level blocks until targetY is passed (targetY < 0: no limit) or
// budgetUs is exhausted (budgetUs <= 0: no limit). Returns true when all
// blocks are done.
bool layoutStep(App& app, float targetY, int64_t budgetUs) {
    auto t0 = Clock::now();
    const auto& children = app.root->children;
    float y = app.layoutCursorY;

    while (app.layoutNextBlock < children.size()) {
        if (targetY >= 0.0f && y > targetY) break;
        if (budgetUs > 0 && usElapsed(t0) > budgetUs) break;

        const auto& child = children[app.layoutNextBlock];
        // Record scroll anchor from source offset
        size_t offset = findFirstSourceOffset(child);
        if (offset != SIZE_MAX) {
            app.scrollAnchors.push_back({offset, y});
        }
        layoutElement(app, child, y, app.layoutIndent, app.layoutMaxWidth);
        app.layoutNextBlock++;
    }

    app.layoutCursorY = y;
    // Partial content height grows as layout fills in (keeps scrollbar sane)
    float scale = app.contentScale * app.zoomFactor;
    app.contentHeight = y + 40.0f * scale;
    return app.layoutNextBlock >= children.size();
}

void layoutFinish(App& app) {
    // Defer toLower to when search actually needs it (lazy rebuild)
    app.docTextLower.clear();
    mapSearchMatchesToLayout(app);
    app.layoutComplete = true;
}

} // namespace

void layoutDocument(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

void layoutDocumentViewportFirst(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        // Lay out through two viewports past the current scroll so the first
        // frame presents immediately; the rest continues in chunks.
        float targetY = app.scrollY + (float)app.height * 2.0f;
        if (layoutStep(app, targetY, -1)) {
            layoutFinish(app);
        }
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

bool layoutDocumentContinue(App& app, int64_t budgetUs) {
    if (app.layoutComplete) return true;
    auto t0 = Clock::now();
    bool done = layoutStep(app, -1.0f, budgetUs);
    if (done) layoutFinish(app);
    app.layoutTimeUs += (size_t)usElapsed(t0);
    return done;
}

void completeAsyncImage(App& app, void* asyncResult) {
    auto* result = static_cast<AsyncImageResult*>(asyncResult);
    if (!result) return;

    App::ImageEntry entry;
    entry.failed = true;
    if (result->ok && app.renderTarget) {
        // No dpi argument: this path takes the 96 default, which is what the
        // D2D1::BitmapProperties one-argument overload it replaces meant. Unlike the
        // SVG path the pixel size IS the intrinsic size here, so pixels and DIPs
        // coincide and there is nothing to declare.
        std::shared_ptr<tinta::Bitmap> bitmap = app.renderTarget->CreateBitmap(
            result->width, result->height, result->pixels.data(), result->width * 4);
        if (bitmap) {
            entry.bitmap = std::move(bitmap);
            entry.width = (int)result->width;
            entry.height = (int)result->height;
            entry.failed = false;
        }
    }

    app.storeImageCacheEntry(result->src, std::move(entry));
    delete result;

    // Reflow with the real image dimensions — but coalesced: several images
    // finishing close together (the common case when a document loads) get
    // one relayout on the timer instead of a full document layout each
    //
    //
    // canArmTimer, not app.hwnd, which is null for the whole of a qinta run: this function
    // runs there too now, from the Qt window's post sink.
    if (canArmTimer()) armTimer(TIMER_IMAGE_REFLOW, 60);
}

void ensureLayoutComplete(App& app) {
    if (app.layoutDirty) {
        layoutDocument(app);
        return;
    }
    if (!app.layoutComplete) {
        auto t0 = Clock::now();
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
        app.layoutTimeUs += (size_t)usElapsed(t0);
    }
}


// Public face of the role resolver for the exporters (the implementation
// lives in this file's anonymous namespace beside the palette helpers)
tinta::Color resolveDiagramRole(const App& app, const mermaidext::Prim& prim,
                                mermaidext::Role role) {
    return resolveDiagramRoleImpl(app, prim, role);
}

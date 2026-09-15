#ifndef TINTA_TEXT_QT_H
#define TINTA_TEXT_QT_H

#include <memory>
#include <vector>

#include "text_api.h"

#include <QFont>
#include <QString>
#include <QTextLayout>

class QPainter;

// The Qt implementation of the text seam.
//
// THE ONE MAPPING THAT IS NOT A CAST, AND WHY IT MATTERS MOST
//
// Read out of the headers rather than recalled, because getting either wrong is
// invisible until someone looks at rendered text:
//
//   QFont::Weight  Thin 100 · ExtraLight 200 · Light 300 · Normal 400 · Medium 500
//                  DemiBold 600 · Bold 700 · ExtraBold 800 · Black 900
//   DWRITE_FONT_WEIGHT  identical numbering, DemiBold spelled SEMI_BOLD
//
// So weight is a straight cast, and text_qt.cpp static_asserts it.
//
//   QFont::Style          StyleNormal 0 · StyleItalic 1 · StyleOblique 2
//   DWRITE_FONT_STYLE     NORMAL 0      · OBLIQUE 1     · ITALIC 2
//
// ITALIC AND OBLIQUE ARE SWAPPED. A cast would compile, produce no warning, and
// silently render all 10 DWRITE_FONT_STYLE_ITALIC sites as a synthesised slant of
// the upright face instead of the font's real italic -- which on a serif body font
// is a visibly worse and quite different letterform. There is no cast in this
// backend for style; there is a switch, and text_qt.cpp asserts the mismatch
// deliberately so nobody "simplifies" it back into a cast.
//
// This is the second backend earning its keep in the way render_d2d.h predicted:
// while Qt was the only implementation there was nothing for the enum to disagree
// with, and while Direct2D was the only one the question could not arise at all.

// The paragraph-level properties a TextFormat carries. Separate from the format
// class because a TextLayout needs its own mutable copy: DirectWrite copies the
// format's properties into the layout at creation, and later SetWordWrapping on
// the layout must not reach back and alter the format 36 other layouts were built
// from.
struct QtTextStyle {
    tinta::TextAlignment textAlignment = tinta::TextAlignment::Leading;
    tinta::ParagraphAlignment paragraphAlignment = tinta::ParagraphAlignment::Near;
    tinta::WordWrapping wordWrapping = tinta::WordWrapping::Wrap;
    tinta::Trimming trimming{tinta::TrimmingGranularity::None, 0, 0};
    bool hasTrimmingSign = false;
    tinta::LineSpacingMethod lineSpacingMethod = tinta::LineSpacingMethod::Default;
    float lineSpacing = 0.0f;
    float baseline = 0.0f;
};

// Text laid out into a box: the shared engine behind GetMetrics, DrawText and
// DrawTextLayout, so that a measured box and a drawn box can never come from two
// different pieces of code.
//
// ALIGNMENT IS COMPUTED HERE, NOT DELEGATED TO QTextOption
//
// Qt can align text itself, and this deliberately does not ask it to. DirectWrite
// defines alignment as an offset of the ink within the layout box, and computing
// that offset explicitly -- from maxWidth minus the line's natural width -- gives
// a definition that provably matches rather than one that probably does. It also
// keeps the paragraph (vertical) case, which QTextOption does not cover at all,
// in the same place as the horizontal one.
class QtTextRun {
public:
    // `formats` carries ranged styling in QTextLayout's OWN indices, already
    // mapped out of caller units by whoever built the list -- QtTextLayout, which
    // is the only thing that owns the map. What this constructor still owes them
    // is the CLAMP: it may elide `text` before laying it out, so a range that was
    // in bounds when the caller set it can be out of bounds by the time it is
    // applied.
    QtTextRun(const QString& text, const QFont& font, const QtTextStyle& style,
              float maxWidth, float maxHeight,
              const std::vector<QTextLayout::FormatRange>& formats);

    QtTextRun(const QtTextRun&) = delete;
    QtTextRun& operator=(const QtTextRun&) = delete;

    const tinta::TextMetrics& metrics() const { return metrics_; }

    // Draws with the painter's current pen colour, at `origin` plus the vertical
    // paragraph offset this run computed.
    void draw(QPainter& painter, const QPointF& origin) const;

    // Exposed for hit testing, which needs the laid-out lines themselves rather
    // than the aggregate metrics. Note that layout().text() is not necessarily the
    // string this run was constructed with -- trimming elides before layout -- so a
    // caller mapping an offset in must clamp against THIS length, not the original.
    const QTextLayout& layout() const { return layout_; }
    qreal paragraphOffset() const { return paragraphOffset_; }

    // Whether the constructor actually shortened the string, as opposed to merely
    // having been asked to. Tracked rather than reported as false, because
    // LineMetrics::isTrimmed is a field no call site reads -- and a field nothing
    // reads is exactly where a convenient zero survives being wrong.
    bool trimmed() const { return trimmed_; }

private:
    QTextLayout layout_;
    tinta::TextMetrics metrics_{};
    qreal paragraphOffset_ = 0.0;
    bool trimmed_ = false;
};

class QtInlineObject final : public tinta::InlineObject {
public:
    // Qt has no inline-object concept and needs none: eliding is a property of a
    // draw call, not an object you build first. So this carries no state, and its
    // whole job is to let a caller distinguish "elide with an ellipsis" from
    // "truncate with nothing", which is the only distinction the 7 SetTrimming
    // sites actually make -- 3 pass a sign and 4 pass nullptr.
    QtInlineObject() = default;
};

class QtTextFormat final : public tinta::TextFormat {
public:
    QtTextFormat(const QFont& font, float fontSize) : font_(font), fontSize_(fontSize) {}

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override { return fontSize_; }

    const QFont& font() const { return font_; }
    const QtTextStyle& style() const { return style_; }

private:
    QFont font_;
    // Stored rather than read back off the QFont. The seam's font size is in DIPs
    // and the QFont carries points, so a round trip through
    // pointSizeF * 96 / 72 would return 15.999999 for a 16 that 24 call sites
    // compare and divide.
    float fontSize_;
    QtTextStyle style_;
};

class QtTextLayout final : public tinta::TextLayout {
public:
    QtTextLayout(const QString& text, const QFont& font, float fontSize,
                 const QtTextStyle& style, float maxWidth, float maxHeight);

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override { return fontSize_; }

    void GetMetrics(tinta::TextMetrics* metrics) const override;

    bool HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                      bool* isInside, tinta::HitTestMetrics* metrics) const override;
    bool HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                             float* pointX, float* pointY,
                             tinta::HitTestMetrics* metrics) const override;
    bool HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                          float originX, float originY,
                          tinta::HitTestMetrics* metrics,
                          uint32_t maxMetricsCount,
                          uint32_t* actualMetricsCount) const override;
    bool GetLineMetrics(tinta::LineMetrics* lineMetrics, uint32_t maxLineCount,
                        uint32_t* actualLineCount) const override;
    bool GetClusterMetrics(tinta::ClusterMetrics* clusterMetrics,
                           uint32_t maxClusterCount,
                           uint32_t* actualClusterCount) const override;

    void SetFontWeight(tinta::FontWeight weight, tinta::TextRange range) override;
    void SetFontSize(float fontSize, tinta::TextRange range) override;
    void SetStrikethrough(bool strikethrough, tinta::TextRange range) override;
    void SetTypography(const tinta::Typography& typography,
                       tinta::TextRange range) override;

    // Exposed so QtRenderTarget::DrawTextLayout can hand it to the same private
    // helper DrawText uses. Without this, DrawTextLayout would have to repeat the
    // pen-versus-brush handling and the Clip translation, and the two copies would
    // be free to drift.
    const QtTextRun& run() const;

private:
    // Every setter invalidates this and the next measurement rebuilds it, which is
    // what DirectWrite does internally when a layout property changes. Doing the
    // work eagerly in each setter would re-lay-out up to four times for the call
    // sites that set alignment, paragraph alignment, wrapping and trimming in a row.
    void ensureRun() const;

    // THE PRICE OF text_api.h's OFFSET CONTRACT, AND IT IS PAID ONLY OFF WINDOWS.
    //
    // The contract is that every offset crossing the seam indexes the CALLER'S
    // wchar_t buffer. text_ is a QString, which is UTF-16 internally. When wchar_t
    // is 16 bits those are the same units and the map below is the identity, costs
    // nothing, and is compiled out. When wchar_t is 32 bits one caller unit is one
    // code point, which is ONE OR TWO QString units, so the two indexings diverge
    // the moment a string contains anything outside the BMP.
    //
    // The map is derived from the QString rather than from the original wchar_t
    // buffer, which is what keeps it a private detail instead of a signature change:
    // a surrogate PAIR in text_ is exactly one caller unit and every other QChar is
    // exactly one, so walking text_ reconstructs the caller's indexing without
    // needing the caller's memory. callerToQt_[i] is the QString index at which
    // caller unit i begins, with one extra entry for the one-past-the-end position
    // -- which is a legal caret location and the reason the vector is size()+1 and
    // not size().
    void ensureIndexMap() const;
    int toQtIndex(uint32_t callerIndex) const;
    uint32_t toCallerIndex(int qtIndex) const;

    // The one place the three ranged setters share: map the range out of caller
    // units, drop it if it is empty or entirely past the end, append it.
    void addRangedFormat(const QTextCharFormat& format, tinta::TextRange range);

    QString text_;
    QFont font_;
    float fontSize_;
    QtTextStyle style_;
    float maxWidth_;
    float maxHeight_;
    mutable std::unique_ptr<QtTextRun> run_;
    // RANGED STYLING, AND WHY IT IS A LIST RATHER THAN A RESOLVED PER-CHARACTER
    // STATE.
    //
    // Indices here are QTextLayout's, mapped at set time -- the map is a property
    // of text_, which never changes, so converting once per setter call is cheaper
    // than converting on every rebuild and cannot go stale.
    //
    // The list is ORDERED AND APPEND-ONLY because that is what reproduces
    // DirectWrite's semantics. DirectWrite resolves overlapping ranged attributes
    // by call order, last writer winning per character per property, and Qt's text
    // engine merges FormatRanges in list order with exactly the same rule
    // (QTextEngine::format merges each matching range over the previous). So
    // appending in call order is not a shortcut around a merge -- it IS the merge,
    // performed by the engine that will render it.
    std::vector<QTextLayout::FormatRange> formats_;
    // Not invalidated by the setters, unlike run_: the map is a property of text_,
    // which is fixed at construction. A layout's box and style change; its string
    // never does, because the seam has no setter for it.
    mutable std::vector<int> callerToQt_;
};

class QtTextFactory final : public tinta::TextFactory {
public:
    std::unique_ptr<tinta::TextFormat> CreateTextFormat(
        const wchar_t* fontFamily, tinta::FontWeight weight, tinta::FontStyle style,
        tinta::FontStretch stretch, float fontSize,
        const wchar_t* localeName) override;

    std::unique_ptr<tinta::TextLayout> CreateTextLayout(
        const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
        float maxWidth, float maxHeight) override;

    std::unique_ptr<tinta::InlineObject> CreateEllipsisTrimmingSign(
        tinta::TextFormat* format) override;

    // Needs no member state at all: QTextBoundaryFinder is constructed per call from
    // the string. The DirectWrite side has to own an IDWriteTextAnalyzer to answer the
    // same question, which is why the seam method is non-const.
    bool AnalyzeLineBreaks(const wchar_t* text, uint32_t length,
                           std::vector<bool>& canBreak) override;
};

// Handles both wchar_t widths, which is the whole reason the seam can keep passing
// wchar_t at all: 16-bit input is UTF-16 and 32-bit input is UTF-32, and
// QString::fromWCharArray already dispatches on sizeof(wchar_t).
//
// text_api.h used to say a pointer-plus-length pair is safe across that difference
// "and an integer offset into text would not be". The second half is now out of
// date, and deliberately so: hit-testing moves integer offsets across the seam in
// both directions, and text_api.h's TextLayout defines what they mean rather than
// leaving it to whichever backend the reader happens to be in. This backend pays for
// that definition in QtTextLayout::ensureIndexMap, which is where the difference
// between a caller unit and a QString unit is reconciled. An offset is safe now
// because something converts it, not because nothing carries one.
QString toQt(const wchar_t* string, uint32_t length);

#endif  // TINTA_TEXT_QT_H

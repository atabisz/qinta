#ifndef TINTA_TEXT_QT_H
#define TINTA_TEXT_QT_H

#include <memory>

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
    QtTextRun(const QString& text, const QFont& font, const QtTextStyle& style,
              float maxWidth, float maxHeight);

    QtTextRun(const QtTextRun&) = delete;
    QtTextRun& operator=(const QtTextRun&) = delete;

    const tinta::TextMetrics& metrics() const { return metrics_; }

    // Draws with the painter's current pen colour, at `origin` plus the vertical
    // paragraph offset this run computed.
    void draw(QPainter& painter, const QPointF& origin) const;

private:
    QTextLayout layout_;
    tinta::TextMetrics metrics_{};
    qreal paragraphOffset_ = 0.0;
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

    QString text_;
    QFont font_;
    float fontSize_;
    QtTextStyle style_;
    float maxWidth_;
    float maxHeight_;
    mutable std::unique_ptr<QtTextRun> run_;
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
};

// Handles both wchar_t widths, which is the whole reason the seam can keep passing
// wchar_t at all: 16-bit input is UTF-16 and 32-bit input is UTF-32, and
// QString::fromWCharArray already dispatches on sizeof(wchar_t). text_api.h has
// the argument for why a pointer-plus-length pair is safe across that difference
// and an integer offset into text would not be.
QString toQt(const wchar_t* string, uint32_t length);

#endif  // TINTA_TEXT_QT_H

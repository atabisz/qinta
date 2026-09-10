#include "text_qt.h"

#include <algorithm>
#include <cmath>

#include <QFontMetricsF>
#include <QPainter>
#include <QTextLine>
#include <QTextOption>

// The pairings text_api.h promises, from the Qt side. render_d2d.cpp and
// text_d2d.cpp make the same assertions against the DirectWrite headers; making
// them from BOTH sides is what turns the enum values from a convention into a
// checked fact, because the two sets of assertions can disagree with each other.

static_assert(static_cast<int>(tinta::FontWeight::Light) == static_cast<int>(QFont::Light),
              "tinta::FontWeight::Light must equal QFont::Light");
static_assert(static_cast<int>(tinta::FontWeight::Normal) == static_cast<int>(QFont::Normal),
              "tinta::FontWeight::Normal must equal QFont::Normal");
static_assert(static_cast<int>(tinta::FontWeight::Medium) == static_cast<int>(QFont::Medium),
              "tinta::FontWeight::Medium must equal QFont::Medium");
static_assert(static_cast<int>(tinta::FontWeight::SemiBold) == static_cast<int>(QFont::DemiBold),
              "tinta::FontWeight::SemiBold must equal QFont::DemiBold");
static_assert(static_cast<int>(tinta::FontWeight::Bold) == static_cast<int>(QFont::Bold),
              "tinta::FontWeight::Bold must equal QFont::Bold");
static_assert(static_cast<int>(tinta::FontWeight::ExtraBold) == static_cast<int>(QFont::ExtraBold),
              "tinta::FontWeight::ExtraBold must equal QFont::ExtraBold");

// DELIBERATELY ASSERTING A MISMATCH. text_qt.h explains it: Qt orders its style
// enum Normal/Italic/Oblique and DirectWrite orders it Normal/Oblique/Italic, so a
// cast silently swaps real italics for synthesised slant at the 10
// DWRITE_FONT_STYLE_ITALIC sites. This assertion fires if a future Qt ever
// reorders to match, which would be the moment the switch below could become a
// cast -- and, much more importantly, it is a comment the compiler enforces, so a
// later reader cannot "tidy" the switch away on the assumption the values line up.
static_assert(static_cast<int>(tinta::FontStyle::Italic) != static_cast<int>(QFont::StyleItalic),
              "Qt and DirectWrite still disagree on italic/oblique ordering; the switch in "
              "toQtStyle() is required and must NOT be replaced with a cast");

namespace {

QFont::Style toQtStyle(tinta::FontStyle style) {
    switch (style) {
        case tinta::FontStyle::Normal:
            return QFont::StyleNormal;
        case tinta::FontStyle::Oblique:
            return QFont::StyleOblique;
        case tinta::FontStyle::Italic:
            return QFont::StyleItalic;
    }
    return QFont::StyleNormal;
}

// DirectWrite sizes fonts in DIPs -- 1/96 inch -- and Qt's point size is 1/72
// inch, so the conversion is exact and not a fudge factor. The seam keeps
// reporting the DIP value from GetFontSize; only the QFont sees points.
qreal dipToPoints(float dip) { return static_cast<qreal>(dip) * 72.0 / 96.0; }

qreal alignmentOffset(tinta::TextAlignment alignment, qreal lineWidth, qreal boxWidth) {
    switch (alignment) {
        case tinta::TextAlignment::Leading:
            return 0.0;
        case tinta::TextAlignment::Trailing:
            return boxWidth - lineWidth;
        case tinta::TextAlignment::Center:
            return (boxWidth - lineWidth) / 2.0;
        case tinta::TextAlignment::Justified:
            // Qt justifies through QTextOption, which this run deliberately does
            // not use for alignment. Justified therefore behaves as Leading here,
            // and NO call site passes it -- DWRITE_TEXT_ALIGNMENT_JUSTIFIED appears
            // nowhere in the tree. Returning 0 rather than asserting keeps a future
            // caller's text visible and slightly wrong instead of absent.
            return 0.0;
    }
    return 0.0;
}

qreal paragraphOffsetFor(tinta::ParagraphAlignment alignment, qreal textHeight,
                         qreal boxHeight) {
    switch (alignment) {
        case tinta::ParagraphAlignment::Near:
            return 0.0;
        case tinta::ParagraphAlignment::Far:
            return boxHeight - textHeight;
        case tinta::ParagraphAlignment::Center:
            return (boxHeight - textHeight) / 2.0;
    }
    return 0.0;
}

QTextOption::WrapMode toQtWrapMode(tinta::WordWrapping wrapping) {
    switch (wrapping) {
        case tinta::WordWrapping::Wrap:
            return QTextOption::WordWrap;
        case tinta::WordWrapping::NoWrap:
            return QTextOption::NoWrap;
        case tinta::WordWrapping::EmergencyBreak:
            return QTextOption::WrapAtWordBoundaryOrAnywhere;
        case tinta::WordWrapping::WholeWord:
            return QTextOption::WordWrap;
        case tinta::WordWrapping::Character:
            return QTextOption::WrapAnywhere;
    }
    return QTextOption::WordWrap;
}

}  // namespace

QString toQt(const wchar_t* string, uint32_t length) {
    if (!string) return QString();
    return QString::fromWCharArray(string, static_cast<int>(length));
}

// --- QtTextRun --------------------------------------------------------------

QtTextRun::QtTextRun(const QString& text, const QFont& font, const QtTextStyle& style,
                     float maxWidth, float maxHeight) {
    const bool noWrap = style.wordWrapping == tinta::WordWrapping::NoWrap;
    const qreal boxWidth = static_cast<qreal>(maxWidth);
    const qreal boxHeight = static_cast<qreal>(maxHeight);

    const QFontMetricsF fm(font);

    QString content = text;

    // Trimming. Qt elides at draw/measure time rather than owning an object for it,
    // so the mapping is to shorten the string before laying it out. Only the
    // single-line case is handled, which is the entire population: all 7
    // SetTrimming sites are labels and list rows that also set NoWrap.
    if (style.trimming.granularity != tinta::TrimmingGranularity::None && noWrap &&
        boxWidth > 0.0) {
        if (fm.horizontalAdvance(content) > boxWidth) {
            content = fm.elidedText(content,
                                    style.hasTrimmingSign ? Qt::ElideRight : Qt::ElideNone,
                                    boxWidth);
        }
    }

    layout_.setText(content);
    layout_.setFont(font);

    QTextOption option;
    option.setWrapMode(toQtWrapMode(style.wordWrapping));
    layout_.setTextOption(option);

    // NoWrap means "one line however wide it is", so the line is given effectively
    // unbounded width rather than the box width. Handing it boxWidth with
    // QTextOption::NoWrap set works in Qt today, and would silently start wrapping
    // if the option were ever lost; this way the intent survives that.
    const qreal lineWidth = noWrap ? std::max<qreal>(boxWidth, 1.0e6) : boxWidth;

    qreal y = 0.0;
    qreal widest = 0.0;
    qreal widestIncludingTrailing = 0.0;
    qreal leftmost = 0.0;
    uint32_t lineCount = 0;

    layout_.beginLayout();
    for (;;) {
        QTextLine line = layout_.createLine();
        if (!line.isValid()) break;

        line.setLineWidth(lineWidth);

        // THE ONE PLACE QT GIVES ONE NUMBER WHERE DIRECTWRITE GIVES TWO.
        //
        // DWRITE_TEXT_METRICS carries `width` (trailing whitespace excluded) and
        // `widthIncludingTrailingWhitespace` (included). The obvious mapping was
        // naturalTextWidth() for the first and horizontalAdvance() for the second,
        // and the shared cross-backend assertion refuted it. Measured, DejaVu Sans at
        // 16 DIP:
        //
        //   "ab"       nowrap  w=1000 -> natural 19.953, advance 19.953
        //   "ab      " nowrap  w=1000 -> natural 50.422, advance 50.422
        //   "ab      " wrap    w=30   -> natural 19.953, advance 19.953
        //
        // The two Qt calls are equal in every case. Qt's model is "the advance, minus
        // whatever whitespace would have overflowed the line", which is neither of
        // DirectWrite's numbers on its own -- it is the first one when the line is
        // constrained and the second when it is not. So the split is computed here.
        //
        // naturalTextWidth() stays authoritative when there IS no trailing
        // whitespace, because it is the shaped width; QFontMetricsF is only reached
        // for the substring case Qt cannot answer. The control that licenses mixing
        // the two: fm.horizontalAdvance("ab") == 19.953 == naturalTextWidth() for the
        // same string, exactly.
        const QString lineText = content.mid(line.textStart(), line.textLength());
        int inkEnd = static_cast<int>(lineText.size());
        while (inkEnd > 0 && lineText.at(inkEnd - 1).isSpace()) inkEnd--;
        const bool hasTrailingSpace = inkEnd < static_cast<int>(lineText.size());

        const qreal natural = hasTrailingSpace ? fm.horizontalAdvance(lineText.left(inkEnd))
                                               : line.naturalTextWidth();
        const qreal advance = hasTrailingSpace ? fm.horizontalAdvance(lineText)
                                               : line.naturalTextWidth();

        // Alignment uses the INK width, not the advance: DirectWrite lets trailing
        // whitespace hang outside the aligned box rather than pushing the glyphs left.
        const qreal offset = alignmentOffset(style.textAlignment, natural, boxWidth);

        line.setPosition(QPointF(offset, y));

        if (lineCount == 0 || offset < leftmost) leftmost = offset;
        widest = std::max(widest, natural);
        widestIncludingTrailing = std::max(widestIncludingTrailing, advance);

        y += line.height();
        lineCount++;
    }
    layout_.endLayout();

    paragraphOffset_ = paragraphOffsetFor(style.paragraphAlignment, y, boxHeight);

    metrics_.left = static_cast<float>(leftmost);
    metrics_.top = static_cast<float>(paragraphOffset_);
    metrics_.width = static_cast<float>(widest);
    metrics_.widthIncludingTrailingWhitespace = static_cast<float>(widestIncludingTrailing);
    metrics_.height = static_cast<float>(y);
    metrics_.layoutWidth = maxWidth;
    metrics_.layoutHeight = maxHeight;
    // Qt does not expose a bidi nesting depth. Reported as 1 -- the value
    // DirectWrite gives for any single-direction paragraph, which every string in
    // this tree is -- rather than 0, which DirectWrite uses for empty text and
    // would therefore be a different claim.
    metrics_.maxBidiReorderingDepth = lineCount > 0 ? 1u : 0u;
    metrics_.lineCount = lineCount;
}

void QtTextRun::draw(QPainter& painter, const QPointF& origin) const {
    layout_.draw(&painter, origin + QPointF(0.0, paragraphOffset_));
}

// --- QtTextFormat -----------------------------------------------------------

void QtTextFormat::SetTextAlignment(tinta::TextAlignment alignment) {
    style_.textAlignment = alignment;
}

void QtTextFormat::SetParagraphAlignment(tinta::ParagraphAlignment alignment) {
    style_.paragraphAlignment = alignment;
}

void QtTextFormat::SetWordWrapping(tinta::WordWrapping wrapping) {
    style_.wordWrapping = wrapping;
}

void QtTextFormat::SetTrimming(const tinta::Trimming& trimming,
                               tinta::InlineObject* trimmingSign) {
    style_.trimming = trimming;
    style_.hasTrimmingSign = trimmingSign != nullptr;
}

void QtTextFormat::SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                                  float baseline) {
    style_.lineSpacingMethod = method;
    style_.lineSpacing = lineSpacing;
    style_.baseline = baseline;
}

// --- QtTextLayout -----------------------------------------------------------

QtTextLayout::QtTextLayout(const QString& text, const QFont& font, float fontSize,
                           const QtTextStyle& style, float maxWidth, float maxHeight)
    : text_(text), font_(font), fontSize_(fontSize), style_(style), maxWidth_(maxWidth),
      maxHeight_(maxHeight) {}

void QtTextLayout::SetTextAlignment(tinta::TextAlignment alignment) {
    style_.textAlignment = alignment;
    run_.reset();
}

void QtTextLayout::SetParagraphAlignment(tinta::ParagraphAlignment alignment) {
    style_.paragraphAlignment = alignment;
    run_.reset();
}

void QtTextLayout::SetWordWrapping(tinta::WordWrapping wrapping) {
    style_.wordWrapping = wrapping;
    run_.reset();
}

void QtTextLayout::SetTrimming(const tinta::Trimming& trimming,
                               tinta::InlineObject* trimmingSign) {
    style_.trimming = trimming;
    style_.hasTrimmingSign = trimmingSign != nullptr;
    run_.reset();
}

void QtTextLayout::SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                                  float baseline) {
    style_.lineSpacingMethod = method;
    style_.lineSpacing = lineSpacing;
    style_.baseline = baseline;
    run_.reset();
}

void QtTextLayout::ensureRun() const {
    if (!run_) {
        run_ = std::make_unique<QtTextRun>(text_, font_, style_, maxWidth_, maxHeight_);
    }
}

void QtTextLayout::GetMetrics(tinta::TextMetrics* metrics) const {
    if (!metrics) return;
    ensureRun();
    *metrics = run_->metrics();
}

const QtTextRun& QtTextLayout::run() const {
    ensureRun();
    return *run_;
}

// --- QtTextFactory ----------------------------------------------------------

std::unique_ptr<tinta::TextFormat> QtTextFactory::CreateTextFormat(
    const wchar_t* fontFamily, tinta::FontWeight weight, tinta::FontStyle style,
    tinta::FontStretch stretch, float fontSize, const wchar_t* localeName) {
    (void)stretch;   // only Normal exists in the seam, and it is QFont's default
    (void)localeName;  // Qt resolves locale through the font database, not per format

    QFont font;
    const QString family = fontFamily ? QString::fromWCharArray(fontFamily) : QString();
    if (!family.isEmpty()) font.setFamily(family);
    font.setPointSizeF(dipToPoints(fontSize));
    font.setWeight(static_cast<QFont::Weight>(static_cast<int>(weight)));
    font.setStyle(toQtStyle(style));

    return std::make_unique<QtTextFormat>(font, fontSize);
}

std::unique_ptr<tinta::TextLayout> QtTextFactory::CreateTextLayout(
    const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
    float maxWidth, float maxHeight) {
    auto* qtFormat = dynamic_cast<QtTextFormat*>(format);
    if (!qtFormat) return nullptr;

    // The style is COPIED out of the format here. DirectWrite does the same, and it
    // is why QtTextLayout holds its own QtTextStyle: a later SetWordWrapping on
    // this layout must not reach back into the format that 35 other layouts share.
    return std::make_unique<QtTextLayout>(toQt(string, stringLength), qtFormat->font(),
                                          qtFormat->GetFontSize(), qtFormat->style(),
                                          maxWidth, maxHeight);
}

std::unique_ptr<tinta::InlineObject> QtTextFactory::CreateEllipsisTrimmingSign(
    tinta::TextFormat* format) {
    // The format is genuinely unused: Qt's elide draws the ellipsis in whatever
    // font the text is already using, so there is nothing to capture. DirectWrite
    // needs it because its trimming sign is a standalone inline object that has to
    // know its own font.
    (void)format;
    return std::make_unique<QtInlineObject>();
}

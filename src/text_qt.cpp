#include "text_qt.h"

#include <algorithm>
#include <cmath>

#include <QFontMetricsF>
#include <QPainter>
#include <QTextCharFormat>
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
                     float maxWidth, float maxHeight,
                     const std::vector<QTextLayout::FormatRange>& formats) {
    const bool noWrap = style.wordWrapping == tinta::WordWrapping::NoWrap;
    const qreal boxWidth = static_cast<qreal>(maxWidth);
    const qreal boxHeight = static_cast<qreal>(maxHeight);

    const QFontMetricsF fm(font);

    QString content = text;

    // NEWLINES, WHICH QT DOES NOT SEE AND DIRECTWRITE BREAKS ON.
    //
    // Measured, DejaVu Sans, QTextLayout with the same createLine() loop below:
    // "ab\ncd" lays out as ONE line of textLength 5 whose naturalTextWidth is
    // 38.91 -- byte-identical to "abcd" -- so '\n' is neither a break nor a
    // glyph to Qt. "ab\ncd\nef" is also one line. DirectWrite gives the first two
    // lines, the first with length 3 and newlineLength 1. That divergence is not
    // hypothetical: mermaid.cpp decodes a literal \n in a node label into a real
    // newline and export_html.cpp lays those labels out.
    //
    // Qt DOES break at U+2028 LINE SEPARATOR, and the resulting line model
    // matches DirectWrite's where it counts -- measured, "ab<U+2028>cd" gives two
    // lines with the first reporting textLength 3, i.e. INCLUDING the separator,
    // which is exactly DirectWrite's length-includes-newlineLength contract. So
    // the whole fix is a substitution, not a layout per paragraph.
    //
    // Three details that are all measured rather than assumed:
    //   - U+2029 PARAGRAPH SEPARATOR does NOT break in Qt. Only U+2028 does, so
    //     this is a property of the specific code point.
    //   - The substitution is 1:1, so the caller-index map below is untouched. A
    //     CRLF collapse would not be, and would need the map to absorb it.
    //   - '\r' is left alone deliberately, and that is what makes CRLF correct
    //     for free: '\r' is invisible and non-breaking to Qt (measured, "ab\r\ncd"
    //     is one line of width 38.91), so "ab\r\ncd" becomes one break whose line
    //     reports textLength 4 -- newlineLength 2, which is DirectWrite's answer
    //     for CRLF. A LONE '\r' therefore does not break here where DirectWrite
    //     would; that is a known divergence, left unasserted rather than papered
    //     over, because nothing in the tree produces one.
    for (int i = 0; i < content.size(); i++) {
        if (content[i] == QLatin1Char('\n')) content[i] = QChar(QChar::LineSeparator);
    }

    // Trimming. Qt elides at draw/measure time rather than owning an object for it,
    // so the mapping is to shorten the string before laying it out. Only the
    // single-line case is handled, which is the entire population: all 7
    // SetTrimming sites are labels and list rows that also set NoWrap.
    if (style.trimming.granularity != tinta::TrimmingGranularity::None && noWrap &&
        boxWidth > 0.0) {
        if (fm.horizontalAdvance(content) > boxWidth) {
            const QString elided =
                fm.elidedText(content,
                              style.hasTrimmingSign ? Qt::ElideRight : Qt::ElideNone,
                              boxWidth);
            trimmed_ = elided != content;
            content = elided;
        }
    }

    layout_.setText(content);
    layout_.setFont(font);

    QTextOption option;
    option.setWrapMode(toQtWrapMode(style.wordWrapping));
    layout_.setTextOption(option);

    // RANGED STYLING, CLAMPED, AND BEFORE beginLayout().
    //
    // Both halves of that matter. Qt reads the formats while it shapes, so a
    // setFormats() after beginLayout() is not late-applied. This comment used to
    // claim it was silently ignored; a mutation that moved the call below
    // beginLayout() was run to check that, and it SEGFAULTS -- Qt 6.4.2, the whole
    // render_api binary, 0.54s in. So the ordering is not a preference and not a
    // subtle metric divergence: get it wrong and there is no binary to measure.
    // Recorded as measured rather than as reasoned, because the reasoned version
    // was wrong in the direction that flatters the tests -- it credited the metric
    // assertions below with catching something that any test at all would catch.
    // What they do catch is the other two mutations: a backend that honours the
    // value and ignores the RANGE, and a missing DIP-to-point conversion. Both of
    // those build and run clean.
    //
    // The clamp is against `content`, not against `text`: the trimming above may
    // have shortened the string, so a range the caller set legitimately can now
    // run past the end. Qt's behaviour on an out-of-bounds FormatRange is not
    // something to find out at a user's expense, and text_api.h promises that
    // styling past the end has no observable effect -- which makes this the line
    // that keeps that promise.
    //
    // ONE HONEST LIMIT, STATED RATHER THAN HIDDEN: the elide above measures with
    // the BASE font, so if a layout were both trimmed and ranged-styled to a
    // larger size, the elide point would be computed for the unstyled text. That
    // combination does not occur -- the trimming sites are labels and list rows,
    // the ranged sites are the six in text_api.h's census, and the two sets are
    // disjoint -- so this is a documented non-case, not a fixed one. It would
    // become real the moment someone styled a trimmed label.
    if (!formats.empty()) {
        QList<QTextLayout::FormatRange> applied;
        for (const QTextLayout::FormatRange& r : formats) {
            if (r.length <= 0 || r.start >= content.size()) continue;
            QTextLayout::FormatRange clamped = r;
            if (clamped.start < 0) clamped.start = 0;
            // FormatRange's fields are `int` and QString::size() is qsizetype, so the
            // min is explicitly widened and then narrowed rather than left to deduce.
            // Written this way because g++ REFUSED to deduce it -- the two candidates
            // are int and long long -- which is the small, useful kind of thing only
            // the second backend's compiler can say, since MSVC never sees this file.
            clamped.length = static_cast<int>(
                std::min<qsizetype>(clamped.length, content.size() - clamped.start));
            applied.append(clamped);
        }
        if (!applied.isEmpty()) layout_.setFormats(applied);
    }

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

// --- QtTextLayout ranged styling --------------------------------------------
//
// Each setter builds a QTextCharFormat carrying EXACTLY ONE property and appends
// it. That is deliberate rather than lazy: a format carrying two properties would
// impose both on every character in its range, so a later SetFontSize over a
// sub-range of an earlier SetFontWeight would silently reset the weight. One
// property per entry makes the engine's merge per-property, which is DirectWrite's
// rule.

void QtTextLayout::addRangedFormat(const QTextCharFormat& format,
                                   tinta::TextRange range) {
    if (range.length == 0) return;

    // Mapped out of caller units here, once, because text_ never changes. The
    // conversion is the identity where wchar_t is 16 bits and real where it is 32.
    ensureIndexMap();

    // CLAMPED IN CALLER UNITS, BEFORE THE CONVERSION, AND BOTH HALVES OF THAT
    // ORDERING ARE LOAD-BEARING.
    //
    // The end position is startPosition + length, and that addition is done in 64
    // bits because `{0, UINT32_MAX}` is a legal DirectWrite way to say "to the end
    // of the text": in 32 bits it wraps to a position BEFORE the start, which
    // neither throws nor warns and just drops the style.
    //
    // It is then clamped against the caller-unit length, and that has to happen
    // BEFORE toQtIndex rather than after. toQtIndex clamps on the 32-bit-wchar_t
    // path and is the bare identity on the 16-bit one -- so a huge caller index
    // comes back out of it unchanged, and `int` is signed, so the very sizes this
    // is guarding against arrive as NEGATIVE Qt indices. Clamping afterwards would
    // have worked on Linux and been wrong on Windows, which is the one asymmetry
    // this backend's own tests cannot see.
    //
    // toCallerIndex(text_.size()) is the caller-unit length on both paths, which is
    // why there is no second spelling of it here.
    const uint32_t callerLength = toCallerIndex(text_.size());
    const uint64_t rawEnd =
        static_cast<uint64_t>(range.startPosition) + static_cast<uint64_t>(range.length);
    const uint32_t callerEnd =
        static_cast<uint32_t>(std::min<uint64_t>(rawEnd, callerLength));
    if (range.startPosition >= callerEnd) return;

    const int start = toQtIndex(range.startPosition);
    const int end = toQtIndex(callerEnd);
    if (end <= start) return;

    QTextLayout::FormatRange entry;
    entry.start = start;
    entry.length = end - start;
    entry.format = format;
    formats_.push_back(entry);
    run_.reset();
}

void QtTextLayout::SetFontWeight(tinta::FontWeight weight, tinta::TextRange range) {
    QTextCharFormat format;
    // The straight cast text_qt.h documents and this file static_asserts, not a
    // second opinion about the numbering.
    format.setFontWeight(static_cast<int>(weight));
    addRangedFormat(format, range);
}

void QtTextLayout::SetFontSize(float fontSize, tinta::TextRange range) {
    QTextCharFormat format;
    // DIPs in, points out -- the same conversion CreateTextFormat applies to the
    // base font. A ranged size that skipped it would be 33% too small and would
    // still look plausible, which is why the assertion for this one compares a
    // styled width against an unstyled one rather than eyeballing a number.
    format.setFontPointSize(dipToPoints(fontSize));
    addRangedFormat(format, range);
}

void QtTextLayout::SetStrikethrough(bool strikethrough, tinta::TextRange range) {
    QTextCharFormat format;
    format.setFontStrikeOut(strikethrough);
    addRangedFormat(format, range);
}

void QtTextLayout::SetTypography(const tinta::Typography& typography,
                                tinta::TextRange range) {
    // What this backend can and cannot honour, and why, is documented once at
    // qtCanHonourFeature in text_api.h -- including that dropping `dlig` is a
    // decision rather than an omission. This function is only the mechanics.
    //
    // Of the three features Qt can honour, exactly ONE has an API: `kern` becomes
    // QTextCharFormat::setFontKerning, which is per-range and so goes through the
    // same addRangedFormat path as every other ranged setter here. `liga` and `calt`
    // are HarfBuzz defaults, already on, and there is nothing to call for them --
    // which is a real difference from being unsupported and is why they are not
    // silently lumped in with `dlig`.
    if (typography.count == 0) return;

    bool sawKerning = false;
    bool enableKerning = false;
    for (size_t i = 0; i < typography.count; ++i) {
        const tinta::FontFeature& f = typography.features[i];
        if (!tinta::qtCanHonourFeature(f)) continue;  // `dlig` leaves here
        const char* t = f.tag;
        if (t[0] == 'k' && t[1] == 'e' && t[2] == 'r' && t[3] == 'n') {
            sawKerning = true;
            enableKerning = f.parameter != 0;
        }
        // liga / calt: on by default in the shaper, nothing to apply.
    }

    // No format is added when the list contained nothing with an API behind it.
    // Adding an empty QTextCharFormat would still create a format range, which
    // costs a shaping segment boundary for no visual effect.
    if (!sawKerning) return;

    QTextCharFormat format;
    format.setFontKerning(enableKerning);
    addRangedFormat(format, range);
}

void QtTextLayout::ensureRun() const {
    if (!run_) {
        run_ = std::make_unique<QtTextRun>(text_, font_, style_, maxWidth_, maxHeight_,
                                          formats_);
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

// --- QtTextLayout hit testing -----------------------------------------------
//
// WHERE THE COORDINATES COME FROM, AND THE ONE THING I DID NOT TAKE ON TRUST
//
// QtTextRun does its own alignment: it computes an offset from the box width and
// calls QTextLine::setPosition, rather than handing Qt a QTextOption alignment. Qt's
// QTextLine::cursorToX and xToCursor are documented in terms of "the line", and
// whether that means the line's own origin or the layout's is exactly the sort of
// thing that is invisible for a Leading-aligned string and wrong by half the box for
// a Centered one. The code below assumes those two functions work in LAYOUT space --
// that setPosition's x is already included -- and that assumption is not left as a
// comment: the shared cross-backend suite asserts that a Centered layout's
// HitTestTextPosition(0) x agrees with GetMetrics().left, on both backends. If Qt
// omitted the offset, that assertion reddens on Qt alone and the repair is to add
// line.position().x() here.
//
// The vertical is NOT in that space and is added explicitly. QtTextRun lays lines
// out from y=0 and applies its paragraph offset at draw time, which is also why
// metrics_.top is the paragraph offset -- so a line's layout-space top is
// paragraphOffset() + line.y(), and every `top` and `pointY` below is built that way.

namespace {

// The fields describing a run of text on ONE line, which is the shape all three
// methods produce. A helper because deriving left/width from two cursor x positions
// is the part that is easy to write three subtly different ways -- and because min
// and fabs rather than (xLo, xHi - xLo) is what keeps it a rectangle if a
// right-to-left run ever makes the later offset the leftmost one.
void fillLineSpan(const QTextLine& line, qreal paragraphOffset, qreal xLo, qreal xHi,
                  uint32_t callerPos, uint32_t callerLength,
                  tinta::HitTestMetrics* out) {
    out->textPosition = callerPos;
    out->length = callerLength;
    out->left = static_cast<float>(std::min(xLo, xHi));
    out->top = static_cast<float>(paragraphOffset + line.y());
    out->width = static_cast<float>(std::fabs(xHi - xLo));
    out->height = static_cast<float>(line.height());
    // Qt exposes a per-LINE direction (QTextLine::textDirection) and not the
    // per-cluster embedding level DWRITE_HIT_TEST_METRICS::bidiLevel carries. The
    // span-27 census put bidiLevel at 0 call sites, alongside isText and isTrimmed,
    // so this reports 0 rather than deriving a level from the line direction --
    // which would be a different claim wearing the right field name.
    out->bidiLevel = 0;
    out->isText = 1;
    out->isTrimmed = 0;
}

}  // namespace

void QtTextLayout::ensureIndexMap() const {
    if constexpr (sizeof(wchar_t) == 2) {
        return;  // a caller unit IS a QString unit; the map is the identity
    } else {
        if (!callerToQt_.empty()) return;
        callerToQt_.reserve(static_cast<size_t>(text_.size()) + 1);
        for (int i = 0; i < text_.size();) {
            callerToQt_.push_back(i);
            // A surrogate PAIR is one caller unit when wchar_t is 32 bits. A LONE
            // surrogate -- which QString permits and real text does not -- is
            // treated as one unit too, so that a malformed string produces a
            // consistent map instead of running off the end.
            if (text_.at(i).isHighSurrogate() && i + 1 < text_.size() &&
                text_.at(i + 1).isLowSurrogate()) {
                i += 2;
            } else {
                i += 1;
            }
        }
        callerToQt_.push_back(text_.size());
    }
}

int QtTextLayout::toQtIndex(uint32_t callerIndex) const {
    if constexpr (sizeof(wchar_t) == 2) {
        return static_cast<int>(callerIndex);
    } else {
        ensureIndexMap();
        if (callerToQt_.empty()) return static_cast<int>(callerIndex);
        const size_t clamped = std::min<size_t>(callerIndex, callerToQt_.size() - 1);
        return callerToQt_[clamped];
    }
}

uint32_t QtTextLayout::toCallerIndex(int qtIndex) const {
    if constexpr (sizeof(wchar_t) == 2) {
        return static_cast<uint32_t>(qtIndex);
    } else {
        ensureIndexMap();
        if (callerToQt_.empty()) return static_cast<uint32_t>(qtIndex);
        // Ascending by construction, so the inverse is a binary search.
        // lower_bound and not upper_bound: an index landing exactly on a caller
        // unit's first QString position must map to THAT unit, and an index landing
        // inside a surrogate pair -- which Qt's cluster-aware cursor movement does
        // not produce, but a clamped or arithmetic index could -- rounds up to the
        // next boundary rather than quietly reporting the pair's start.
        const auto it = std::lower_bound(callerToQt_.begin(), callerToQt_.end(), qtIndex);
        if (it == callerToQt_.end()) return static_cast<uint32_t>(callerToQt_.size() - 1);
        return static_cast<uint32_t>(it - callerToQt_.begin());
    }
}

bool QtTextLayout::HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                                bool* isInside, tinta::HitTestMetrics* metrics) const {
    // Outputs zeroed before any early return, the same discipline as the Direct2D
    // side and for the same measured reason: 5 of the 13 call sites ignore the
    // return value and read the metrics regardless.
    if (isTrailingHit) *isTrailingHit = false;
    if (isInside) *isInside = false;
    if (metrics) *metrics = tinta::HitTestMetrics{};

    ensureRun();
    const QTextLayout& layout = run_->layout();
    const int lineCount = layout.lineCount();
    if (lineCount == 0) return false;

    const qreal paragraphOffset = run_->paragraphOffset();
    const qreal y = static_cast<qreal>(pointY) - paragraphOffset;

    // NEAREST line, not strictly-containing line. A point above or below the text
    // has to answer with the first or last line and isInside=false rather than
    // refusing: that case is a drag-select running past the end of the text, which
    // is what selection.cpp's 5 sites do for a living.
    int index = lineCount - 1;
    for (int i = 0; i < lineCount; ++i) {
        const QTextLine candidate = layout.lineAt(i);
        if (y < candidate.y() + candidate.height()) {
            index = i;
            break;
        }
    }

    const QTextLine line = layout.lineAt(index);
    if (!line.isValid()) return false;

    // TWO xToCursor CALLS, AND THEY ARE NOT REDUNDANT.
    //
    // CursorOnCharacter gives the cluster the point is inside. CursorBetweenCharacters
    // snaps to the nearer cluster BOUNDARY, so it is one cluster further along once
    // the point passes the midpoint. DirectWrite reports exactly that pair -- the
    // cluster, plus a flag for which half of it was hit -- and one call cannot
    // reconstruct it: a single call answers "which cluster" or "which boundary",
    // never both, and the flag is the difference between placing a caret before a
    // character and after it.
    const int qtOn =
        line.xToCursor(static_cast<qreal>(pointX), QTextLine::CursorOnCharacter);
    const int qtBetween =
        line.xToCursor(static_cast<qreal>(pointX), QTextLine::CursorBetweenCharacters);

    const int qtLength = layout.text().size();
    const int qtPos = std::min(qtOn, qtBetween);
    int qtEnd = layout.nextCursorPosition(qtPos);
    if (qtEnd < qtPos || qtEnd > qtLength) qtEnd = qtPos;

    const qreal xLo = line.cursorToX(qtPos);
    const qreal xHi = line.cursorToX(qtEnd);

    if (isTrailingHit) *isTrailingHit = qtBetween > qtOn;
    if (isInside) {
        const qreal left = std::min(xLo, xHi);
        const qreal right = std::max(xLo, xHi);
        *isInside = static_cast<qreal>(pointX) >= left &&
                    static_cast<qreal>(pointX) < right && y >= line.y() &&
                    y < line.y() + line.height();
    }
    if (metrics) {
        fillLineSpan(line, paragraphOffset, xLo, xHi, toCallerIndex(qtPos),
                     toCallerIndex(qtEnd) - toCallerIndex(qtPos), metrics);
    }
    return true;
}

bool QtTextLayout::HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                                       float* pointX, float* pointY,
                                       tinta::HitTestMetrics* metrics) const {
    if (pointX) *pointX = 0.0f;
    if (pointY) *pointY = 0.0f;
    if (metrics) *metrics = tinta::HitTestMetrics{};

    ensureRun();
    const QTextLayout& layout = run_->layout();
    if (layout.lineCount() == 0) return false;

    // Clamped against the LAID-OUT string and not against text_. QtTextRun's
    // constructor elides before laying out, so a trimmed layout's lines cover fewer
    // characters than the layout was constructed with, and an offset past the
    // elision point has no glyph to report. Clamping answers with the last real
    // position; not clamping would ask Qt for a line that does not exist. (No call
    // site combines trimming with hit-testing today -- all 7 SetTrimming sites are
    // labels -- so this is a guard, not a behaviour anything depends on.)
    const int qtLength = layout.text().size();
    const int qtPos = std::clamp(toQtIndex(textPosition), 0, qtLength);

    QTextLine line = layout.lineForTextPosition(qtPos);
    if (!line.isValid()) line = layout.lineAt(layout.lineCount() - 1);
    if (!line.isValid()) return false;

    // A trailing hit is the FAR EDGE OF THE CLUSTER at this position, not qtPos + 1.
    // nextCursorPosition is cluster-aware, which is the entire reason it is used:
    // +1 lands inside a surrogate pair or a combining sequence and asks for an x
    // that Qt itself considers a non-position.
    int qtEnd = layout.nextCursorPosition(qtPos);
    if (qtEnd < qtPos || qtEnd > qtLength) qtEnd = qtPos;

    const qreal xLo = line.cursorToX(qtPos);
    const qreal xHi = line.cursorToX(qtEnd);

    if (pointX) *pointX = static_cast<float>(isTrailingHit ? xHi : xLo);
    if (pointY) *pointY = static_cast<float>(run_->paragraphOffset() + line.y());
    if (metrics) {
        fillLineSpan(line, run_->paragraphOffset(), xLo, xHi, toCallerIndex(qtPos),
                     toCallerIndex(qtEnd) - toCallerIndex(qtPos), metrics);
    }
    return true;
}

bool QtTextLayout::HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                                    float originX, float originY,
                                    tinta::HitTestMetrics* metrics,
                                    uint32_t maxMetricsCount,
                                    uint32_t* actualMetricsCount) const {
    if (actualMetricsCount) *actualMetricsCount = 0;

    ensureRun();
    const QTextLayout& layout = run_->layout();
    const int lineCount = layout.lineCount();
    if (lineCount == 0) return false;

    const int qtLength = layout.text().size();
    const int qtLo = std::clamp(toQtIndex(textPosition), 0, qtLength);
    // Clamped low at qtLo, which also absorbs a textPosition + textLength that wraps
    // uint32: the sum comes back smaller, clamps up to qtLo, and the range reads as
    // empty -- a count of 0 rather than a walk over a nonsense span.
    const int qtHi = std::clamp(toQtIndex(textPosition + textLength), qtLo, qtLength);

    const qreal paragraphOffset = run_->paragraphOffset();

    // AN EMPTY RANGE IS A CARET, NOT NOTHING -- AND THIS IS MEASURED, NOT ASSUMED.
    //
    // I wrote this backend to answer a zero-length range with a count of 0, on the
    // reasoning that no characters means no rectangles, and asserted that shape in the
    // shared suite. It passed here and reddened on Direct2D, which is the entire
    // reason the suite runs one assertion set against both. What DirectWrite actually
    // returns for HitTestTextRange(2, 0, ...) is ONE metric: textPosition 2, length 0,
    // width 0, height the full line height -- the caret rectangle at that offset.
    //
    // DirectWrite wins, and not by seniority: it is the behaviour the editor's
    // selection drawing already sees today, so a Qt backend answering 0 would be a
    // silent behaviour change on exactly the path this seam exists to preserve. A
    // zero-width rectangle also carries strictly more information than an empty
    // count -- it says WHERE the empty selection is.
    if (qtHi == qtLo) {
        QTextLine line = layout.lineForTextPosition(qtLo);
        if (!line.isValid()) line = layout.lineAt(lineCount - 1);
        if (!line.isValid()) return false;
        if (metrics && maxMetricsCount >= 1) {
            const qreal x = line.cursorToX(qtLo);
            fillLineSpan(line, paragraphOffset, x, x, toCallerIndex(qtLo), 0, &metrics[0]);
            metrics[0].left += originX;
            metrics[0].top += originY;
        }
        if (actualMetricsCount) *actualMetricsCount = 1;
        // False for the count-probe pass and true once there is room for the one
        // rectangle, which is the same protocol the loop below follows.
        return maxMetricsCount >= 1;
    }

    // ONE METRIC PER LINE THE RANGE TOUCHES, which is DirectWrite's contract and the
    // reason the count cannot be known before the walk: a two-character range is one
    // rectangle or two depending entirely on where the layout happened to wrap.
    uint32_t produced = 0;
    for (int i = 0; i < lineCount; ++i) {
        const QTextLine line = layout.lineAt(i);
        if (!line.isValid()) continue;

        const int lineLo = line.textStart();
        const int lineHi = lineLo + line.textLength();
        const int lo = std::max(qtLo, lineLo);
        const int hi = std::min(qtHi, lineHi);
        if (lo >= hi) continue;

        // `produced` is incremented outside the buffer check, not inside it. The
        // count-probe pass passes maxMetricsCount == 0 on purpose -- editor.cpp:2792
        // sizes a vector from it -- so folding the increment into the write would
        // answer that pass with 0 and the caller would conclude the range is empty
        // and draw no selection at all.
        if (metrics && produced < maxMetricsCount) {
            const qreal xLo = line.cursorToX(lo);
            const qreal xHi = line.cursorToX(hi);
            tinta::HitTestMetrics& out = metrics[produced];
            fillLineSpan(line, paragraphOffset, xLo, xHi, toCallerIndex(lo),
                         toCallerIndex(hi) - toCallerIndex(lo), &out);
            out.left += originX;
            out.top += originY;
        }
        produced++;
    }

    if (actualMetricsCount) *actualMetricsCount = produced;

    // False when the buffer could not take them all, mirroring the
    // E_NOT_SUFFICIENT_BUFFER the Direct2D side turns into false, so the two
    // backends agree on the two-pass PROTOCOL and not merely on the successful pass.
    // An empty range is a SUCCESS with a count of 0: the caller has to be able to
    // tell "nothing selected" from "ask again with a bigger buffer".
    return produced <= maxMetricsCount;
}

bool QtTextLayout::GetLineMetrics(tinta::LineMetrics* lineMetrics,
                                 uint32_t maxLineCount,
                                 uint32_t* actualLineCount) const {
    if (actualLineCount) *actualLineCount = 0;

    ensureRun();
    const QTextLayout& layout = run_->layout();
    const QString& laid = layout.text();
    const uint32_t lineCount = static_cast<uint32_t>(std::max(0, layout.lineCount()));
    if (actualLineCount) *actualLineCount = lineCount;

    // Same two-pass protocol as HitTestTextRange, same reason: the count has to be
    // answerable without a buffer, because export_html.cpp sizes its vector from
    // the probe pass before it ever allocates one.
    if (!lineMetrics || lineCount > maxLineCount) return lineCount <= maxLineCount;

    for (uint32_t i = 0; i < lineCount; i++) {
        const QTextLine line = layout.lineAt(static_cast<int>(i));
        const int start = line.textStart();
        const int end = start + line.textLength();

        tinta::LineMetrics& out = lineMetrics[i];
        out = tinta::LineMetrics{};

        // Every length below goes through toCallerIndex, per text_api.h's offset
        // contract. It is the identity on Windows and for the newline characters
        // themselves -- they are all BMP -- but computing it rather than assuming
        // it is what keeps a non-BMP line correct off Windows.
        out.length = toCallerIndex(end) - toCallerIndex(start);

        // newlineLength counts the characters that ENDED this line, and the
        // constructor's substitution is what makes this readable at all: '\n'
        // became U+2028, '\r' was left as-is, so a CRLF-terminated line ends with
        // "\r<U+2028>" and reports 2 -- DirectWrite's answer. Nothing else in the
        // string can be U+2028, because the substitution is the only writer of it.
        int nlStart = end;
        if (end > start && end <= laid.size() &&
            laid[end - 1] == QChar(QChar::LineSeparator)) {
            nlStart = end - 1;
            if (nlStart > start && laid[nlStart - 1] == QLatin1Char('\r')) nlStart--;
        }
        out.newlineLength = toCallerIndex(end) - toCallerIndex(nlStart);

        // DirectWrite counts the newline sequence as trailing whitespace, so this
        // walks back from the newline rather than from the end of the line.
        int wsStart = nlStart;
        while (wsStart > start && laid[wsStart - 1].isSpace()) wsStart--;
        out.trailingWhitespaceLength = toCallerIndex(end) - toCallerIndex(wsStart);

        out.height = static_cast<float>(line.height());
        // Qt's ascent IS the baseline distance from the line's top, which is what
        // DirectWrite's `baseline` means. The three sites that read this use it as
        // an ascent (math_render.cpp assigns it to m.ascent and derives descent as
        // height - baseline), so the two names describe the same number.
        out.baseline = static_cast<float>(line.ascent());
        out.isTrimmed = run_->trimmed() ? 1 : 0;
    }

    return true;
}

bool QtTextLayout::GetClusterMetrics(tinta::ClusterMetrics* clusterMetrics,
                                     uint32_t maxClusterCount,
                                     uint32_t* actualClusterCount) const {
    if (actualClusterCount) *actualClusterCount = 0;

    ensureRun();
    const QTextLayout& layout = run_->layout();
    const QString& laid = layout.text();

    // Qt has no cluster API, so clusters are the CURSOR POSITIONS -- which is not
    // a substitute for one but the same thing under a different name:
    // nextCursorPosition is grapheme-aware, so it steps over a surrogate pair or a
    // combining sequence exactly where DirectWrite would put a cluster boundary.
    // The walk is per line rather than over the whole string because cursorToX is
    // a QTextLine method, and a cluster never straddles a line.
    uint32_t produced = 0;

    for (int li = 0; li < layout.lineCount(); li++) {
        const QTextLine line = layout.lineAt(li);
        const int start = line.textStart();
        const int end = start + line.textLength();

        int p = start;
        while (p < end) {
            int next = layout.nextCursorPosition(p);
            // Clamp to the line: a cursor step must not carry the walk past the
            // line's own end, or two lines would report overlapping clusters and
            // the lengths would stop summing to the text length.
            if (next <= p) next = p + 1;
            if (next > end) next = end;

            if (clusterMetrics && produced < maxClusterCount) {
                tinta::ClusterMetrics& out = clusterMetrics[produced];
                out = tinta::ClusterMetrics{};
                out.width =
                    static_cast<float>(line.cursorToX(next) - line.cursorToX(p));
                out.length = static_cast<uint16_t>(toCallerIndex(next) -
                                                   toCallerIndex(p));

                // Only the two flags that are properties of the CHARACTER are
                // filled. canWrapLineAfter, isSoftHyphen and isRightToLeft are
                // properties of the shaping and bidi run structure, which Qt does
                // not expose here, and no call site reads any of the five -- so a
                // derived-looking value for those three would be an unasserted
                // semantic dressed up as a feature. These two are filled because
                // they are cheap AND honest, and because isWhitespace is the one
                // runtime check the Direct2D side's bit-field cast has.
                const QChar ch = (p < laid.size()) ? laid[p] : QChar();
                out.isNewline = (ch == QChar(QChar::LineSeparator) ||
                                 ch == QLatin1Char('\r')) ? 1u : 0u;
                out.isWhitespace = ch.isSpace() ? 1u : 0u;
            }
            // Incremented outside the buffer check, for the same reason
            // HitTestTextRange's `produced` is: the count-probe pass arrives with
            // maxClusterCount == 0 and render.cpp sizes its vector from the answer.
            produced++;
            p = next;
        }
    }

    if (actualClusterCount) *actualClusterCount = produced;
    // False when the buffer could not take them all, INCLUDING the probe pass,
    // which is what DirectWrite's E_NOT_SUFFICIENT_BUFFER turns into on the other
    // side. Returning true for the probe would make the two backends disagree on
    // the protocol while agreeing on every value it produced.
    return produced <= maxClusterCount;
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

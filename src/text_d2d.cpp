#include "text_d2d.h"

#include <cassert>
#include <cstddef>

// The pairings text_api.h promises but cannot assert, because that header must
// stay clear of <dwrite.h>. Every value below was read out of the SDK header
// rather than recalled, and five of these enums turned out to have IMPLICIT values
// -- DWRITE_FONT_STYLE, DWRITE_TEXT_ALIGNMENT, DWRITE_PARAGRAPH_ALIGNMENT,
// DWRITE_TRIMMING_GRANULARITY and DWRITE_LINE_SPACING_METHOD are all declared with
// bare enumerators and no `= N` anywhere. That makes their DECLARATION ORDER the
// entire contract, which is exactly the kind of thing that is safe today, silently
// reorderable by a future SDK, and impossible to notice by reading tinta. So these
// assertions are not ceremony: for those five enums they are the only thing
// standing between a reordered SDK header and text that centres itself when it was
// asked to align left.

#define TINTA_ASSERT_ENUM(tintaValue, dwriteValue)                             \
    static_assert(static_cast<int>(tintaValue) == static_cast<int>(dwriteValue), \
                  #tintaValue " must equal " #dwriteValue)

TINTA_ASSERT_ENUM(tinta::FontWeight::Light, DWRITE_FONT_WEIGHT_LIGHT);
TINTA_ASSERT_ENUM(tinta::FontWeight::Normal, DWRITE_FONT_WEIGHT_NORMAL);
TINTA_ASSERT_ENUM(tinta::FontWeight::Medium, DWRITE_FONT_WEIGHT_MEDIUM);
TINTA_ASSERT_ENUM(tinta::FontWeight::SemiBold, DWRITE_FONT_WEIGHT_SEMI_BOLD);
TINTA_ASSERT_ENUM(tinta::FontWeight::Bold, DWRITE_FONT_WEIGHT_BOLD);
TINTA_ASSERT_ENUM(tinta::FontWeight::ExtraBold, DWRITE_FONT_WEIGHT_EXTRA_BOLD);

TINTA_ASSERT_ENUM(tinta::FontStyle::Normal, DWRITE_FONT_STYLE_NORMAL);
TINTA_ASSERT_ENUM(tinta::FontStyle::Oblique, DWRITE_FONT_STYLE_OBLIQUE);
TINTA_ASSERT_ENUM(tinta::FontStyle::Italic, DWRITE_FONT_STYLE_ITALIC);

TINTA_ASSERT_ENUM(tinta::FontStretch::Normal, DWRITE_FONT_STRETCH_NORMAL);

TINTA_ASSERT_ENUM(tinta::TextAlignment::Leading, DWRITE_TEXT_ALIGNMENT_LEADING);
TINTA_ASSERT_ENUM(tinta::TextAlignment::Trailing, DWRITE_TEXT_ALIGNMENT_TRAILING);
TINTA_ASSERT_ENUM(tinta::TextAlignment::Center, DWRITE_TEXT_ALIGNMENT_CENTER);
TINTA_ASSERT_ENUM(tinta::TextAlignment::Justified, DWRITE_TEXT_ALIGNMENT_JUSTIFIED);

TINTA_ASSERT_ENUM(tinta::ParagraphAlignment::Near, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
TINTA_ASSERT_ENUM(tinta::ParagraphAlignment::Far, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
TINTA_ASSERT_ENUM(tinta::ParagraphAlignment::Center, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

TINTA_ASSERT_ENUM(tinta::WordWrapping::Wrap, DWRITE_WORD_WRAPPING_WRAP);
TINTA_ASSERT_ENUM(tinta::WordWrapping::NoWrap, DWRITE_WORD_WRAPPING_NO_WRAP);
TINTA_ASSERT_ENUM(tinta::WordWrapping::EmergencyBreak, DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
TINTA_ASSERT_ENUM(tinta::WordWrapping::WholeWord, DWRITE_WORD_WRAPPING_WHOLE_WORD);
TINTA_ASSERT_ENUM(tinta::WordWrapping::Character, DWRITE_WORD_WRAPPING_CHARACTER);

TINTA_ASSERT_ENUM(tinta::TrimmingGranularity::None, DWRITE_TRIMMING_GRANULARITY_NONE);
TINTA_ASSERT_ENUM(tinta::TrimmingGranularity::Character, DWRITE_TRIMMING_GRANULARITY_CHARACTER);
TINTA_ASSERT_ENUM(tinta::TrimmingGranularity::Word, DWRITE_TRIMMING_GRANULARITY_WORD);

TINTA_ASSERT_ENUM(tinta::LineSpacingMethod::Default, DWRITE_LINE_SPACING_METHOD_DEFAULT);
TINTA_ASSERT_ENUM(tinta::LineSpacingMethod::Uniform, DWRITE_LINE_SPACING_METHOD_UNIFORM);

// --- Layout claim -----------------------------------------------------------
//
// A correction to the reasoning these were first written down with. I claimed they
// were what licenses a call site to change `DWRITE_TEXT_METRICS m` into
// `tinta::TextMetrics m` as a one-word edit. That is not true: the call site passes
// `&m` to GetMetrics and then reads `m.width`, so what makes the edit safe is that
// the FIELD NAMES match, and no layout property is involved. Both conversions
// below copy field by field for the reason geometry.h gives -- a copy needs no
// aliasing argument to be correct.
//
// They are kept anyway, with an honest job description: they are a drift alarm. If
// a future SDK reorders DWRITE_TEXT_METRICS or widens a field, the field-by-field
// copies stay correct and nothing anywhere would report the change. These turn it
// into a build failure instead.

static_assert(sizeof(tinta::TextMetrics) == sizeof(DWRITE_TEXT_METRICS),
              "tinta::TextMetrics must match DWRITE_TEXT_METRICS");
static_assert(offsetof(tinta::TextMetrics, left) == offsetof(DWRITE_TEXT_METRICS, left), "left");
static_assert(offsetof(tinta::TextMetrics, top) == offsetof(DWRITE_TEXT_METRICS, top), "top");
static_assert(offsetof(tinta::TextMetrics, width) == offsetof(DWRITE_TEXT_METRICS, width), "width");
static_assert(offsetof(tinta::TextMetrics, widthIncludingTrailingWhitespace) ==
                  offsetof(DWRITE_TEXT_METRICS, widthIncludingTrailingWhitespace),
              "widthIncludingTrailingWhitespace");
static_assert(offsetof(tinta::TextMetrics, height) == offsetof(DWRITE_TEXT_METRICS, height), "height");
static_assert(offsetof(tinta::TextMetrics, layoutWidth) ==
                  offsetof(DWRITE_TEXT_METRICS, layoutWidth), "layoutWidth");
static_assert(offsetof(tinta::TextMetrics, layoutHeight) ==
                  offsetof(DWRITE_TEXT_METRICS, layoutHeight), "layoutHeight");
static_assert(offsetof(tinta::TextMetrics, maxBidiReorderingDepth) ==
                  offsetof(DWRITE_TEXT_METRICS, maxBidiReorderingDepth), "maxBidiReorderingDepth");
static_assert(offsetof(tinta::TextMetrics, lineCount) ==
                  offsetof(DWRITE_TEXT_METRICS, lineCount), "lineCount");

static_assert(sizeof(tinta::Trimming) == sizeof(DWRITE_TRIMMING),
              "tinta::Trimming must match DWRITE_TRIMMING");
static_assert(offsetof(tinta::Trimming, granularity) ==
                  offsetof(DWRITE_TRIMMING, granularity), "granularity");
static_assert(offsetof(tinta::Trimming, delimiter) ==
                  offsetof(DWRITE_TRIMMING, delimiter), "delimiter");
static_assert(offsetof(tinta::Trimming, delimiterCount) ==
                  offsetof(DWRITE_TRIMMING, delimiterCount), "delimiterCount");

namespace {

IDWriteInlineObject* nativeSign(tinta::InlineObject* sign) {
    if (!sign) return nullptr;
    assert(dynamic_cast<D2DInlineObject*>(sign) != nullptr &&
           "a non-DirectWrite InlineObject reached the Direct2D text backend");
    return static_cast<D2DInlineObject*>(sign)->native();
}

}  // namespace

namespace tinta_d2d_text {

void setTextAlignment(IDWriteTextFormat* format, tinta::TextAlignment alignment) {
    if (format) format->SetTextAlignment(static_cast<DWRITE_TEXT_ALIGNMENT>(alignment));
}

void setParagraphAlignment(IDWriteTextFormat* format, tinta::ParagraphAlignment alignment) {
    if (format) {
        format->SetParagraphAlignment(static_cast<DWRITE_PARAGRAPH_ALIGNMENT>(alignment));
    }
}

void setWordWrapping(IDWriteTextFormat* format, tinta::WordWrapping wrapping) {
    if (format) format->SetWordWrapping(static_cast<DWRITE_WORD_WRAPPING>(wrapping));
}

void setTrimming(IDWriteTextFormat* format, const tinta::Trimming& trimming,
                 tinta::InlineObject* trimmingSign) {
    if (!format) return;
    DWRITE_TRIMMING native{};
    native.granularity = static_cast<DWRITE_TRIMMING_GRANULARITY>(trimming.granularity);
    native.delimiter = trimming.delimiter;
    native.delimiterCount = trimming.delimiterCount;
    format->SetTrimming(&native, nativeSign(trimmingSign));
}

void setLineSpacing(IDWriteTextFormat* format, tinta::LineSpacingMethod method,
                    float lineSpacing, float baseline) {
    if (format) {
        format->SetLineSpacing(static_cast<DWRITE_LINE_SPACING_METHOD>(method), lineSpacing,
                               baseline);
    }
}

float getFontSize(const IDWriteTextFormat* format) {
    // GetFontSize is not const in DirectWrite, and cannot be: it is a COM
    // interface method. The seam declares GetFontSize() const because all 24 call
    // sites are reads of a long-lived App member, and const_cast here is the
    // narrowest possible place to reconcile the two -- one line, in one file,
    // rather than 24 non-const handles.
    if (!format) return 0.0f;
    return const_cast<IDWriteTextFormat*>(format)->GetFontSize();
}

}  // namespace tinta_d2d_text

// --- D2DInlineObject --------------------------------------------------------

D2DInlineObject::~D2DInlineObject() {
    if (object_) object_->Release();
}

// --- D2DTextFormat ----------------------------------------------------------

D2DTextFormat::~D2DTextFormat() {
    if (format_) format_->Release();
}

void D2DTextFormat::SetTextAlignment(tinta::TextAlignment alignment) {
    tinta_d2d_text::setTextAlignment(format_, alignment);
}

void D2DTextFormat::SetParagraphAlignment(tinta::ParagraphAlignment alignment) {
    tinta_d2d_text::setParagraphAlignment(format_, alignment);
}

void D2DTextFormat::SetWordWrapping(tinta::WordWrapping wrapping) {
    tinta_d2d_text::setWordWrapping(format_, wrapping);
}

void D2DTextFormat::SetTrimming(const tinta::Trimming& trimming,
                                tinta::InlineObject* trimmingSign) {
    tinta_d2d_text::setTrimming(format_, trimming, trimmingSign);
}

void D2DTextFormat::SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                                   float baseline) {
    tinta_d2d_text::setLineSpacing(format_, method, lineSpacing, baseline);
}

float D2DTextFormat::GetFontSize() const { return tinta_d2d_text::getFontSize(format_); }

// --- D2DTextLayout ----------------------------------------------------------
//
// Every format method forwards to the same free helper D2DTextFormat uses, with
// layout_ converting to IDWriteTextFormat* through DirectWrite's own inheritance.

D2DTextLayout::~D2DTextLayout() {
    if (layout_) layout_->Release();
}

void D2DTextLayout::SetTextAlignment(tinta::TextAlignment alignment) {
    tinta_d2d_text::setTextAlignment(layout_, alignment);
}

void D2DTextLayout::SetParagraphAlignment(tinta::ParagraphAlignment alignment) {
    tinta_d2d_text::setParagraphAlignment(layout_, alignment);
}

void D2DTextLayout::SetWordWrapping(tinta::WordWrapping wrapping) {
    tinta_d2d_text::setWordWrapping(layout_, wrapping);
}

void D2DTextLayout::SetTrimming(const tinta::Trimming& trimming,
                                tinta::InlineObject* trimmingSign) {
    tinta_d2d_text::setTrimming(layout_, trimming, trimmingSign);
}

void D2DTextLayout::SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                                   float baseline) {
    tinta_d2d_text::setLineSpacing(layout_, method, lineSpacing, baseline);
}

float D2DTextLayout::GetFontSize() const { return tinta_d2d_text::getFontSize(layout_); }

void D2DTextLayout::GetMetrics(tinta::TextMetrics* metrics) const {
    if (!metrics) return;
    *metrics = tinta::TextMetrics{};
    if (!layout_) return;

    DWRITE_TEXT_METRICS native{};
    if (FAILED(const_cast<IDWriteTextLayout*>(layout_)->GetMetrics(&native))) return;

    metrics->left = native.left;
    metrics->top = native.top;
    metrics->width = native.width;
    metrics->widthIncludingTrailingWhitespace = native.widthIncludingTrailingWhitespace;
    metrics->height = native.height;
    metrics->layoutWidth = native.layoutWidth;
    metrics->layoutHeight = native.layoutHeight;
    metrics->maxBidiReorderingDepth = native.maxBidiReorderingDepth;
    metrics->lineCount = native.lineCount;
}

// --- D2DTextFactory ---------------------------------------------------------

std::unique_ptr<tinta::TextFormat> D2DTextFactory::CreateTextFormat(
    const wchar_t* fontFamily, tinta::FontWeight weight, tinta::FontStyle style,
    tinta::FontStretch stretch, float fontSize, const wchar_t* localeName) {
    if (!factory_) return nullptr;

    IDWriteTextFormat* format = nullptr;
    const HRESULT hr = factory_->CreateTextFormat(
        fontFamily, nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight),
        static_cast<DWRITE_FONT_STYLE>(style), static_cast<DWRITE_FONT_STRETCH>(stretch),
        fontSize, localeName ? localeName : L"", &format);

    // DirectWrite can return a success code with a null out-parameter under
    // memory pressure, and every one of the 37 call sites already tests the
    // pointer rather than the HRESULT. Testing both means a caller never has to
    // ask which of the two this backend believed.
    if (FAILED(hr) || !format) {
        if (format) format->Release();
        return nullptr;
    }
    return std::make_unique<D2DTextFormat>(format);
}

std::unique_ptr<tinta::TextLayout> D2DTextFactory::CreateTextLayout(
    const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
    float maxWidth, float maxHeight) {
    if (!factory_) return nullptr;
    IDWriteTextFormat* nativeFormat = nativeTextFormat(format);
    if (!nativeFormat) return nullptr;

    IDWriteTextLayout* layout = nullptr;
    const HRESULT hr = factory_->CreateTextLayout(string, stringLength, nativeFormat,
                                                 maxWidth, maxHeight, &layout);
    if (FAILED(hr) || !layout) {
        if (layout) layout->Release();
        return nullptr;
    }
    return std::make_unique<D2DTextLayout>(layout);
}

std::unique_ptr<tinta::InlineObject> D2DTextFactory::CreateEllipsisTrimmingSign(
    tinta::TextFormat* format) {
    if (!factory_) return nullptr;
    IDWriteTextFormat* nativeFormat = nativeTextFormat(format);
    if (!nativeFormat) return nullptr;

    IDWriteInlineObject* sign = nullptr;
    const HRESULT hr = factory_->CreateEllipsisTrimmingSign(nativeFormat, &sign);
    if (FAILED(hr) || !sign) {
        if (sign) sign->Release();
        return nullptr;
    }
    return std::make_unique<D2DInlineObject>(sign);
}

// --- unwrapping -------------------------------------------------------------

// Has to try BOTH concrete classes, and this is the one place the decision not to
// mirror the interface inheritance in the concrete classes costs anything. A
// tinta::TextFormat* may be a D2DTextFormat or -- because tinta::TextLayout
// derives from tinta::TextFormat -- a D2DTextLayout, and those two share no
// concrete base. Five lines here, against virtual inheritance in the portable
// header, is the trade text_d2d.h describes.
IDWriteTextFormat* nativeTextFormat(tinta::TextFormat* format) {
    if (!format) return nullptr;
    if (auto* plain = dynamic_cast<D2DTextFormat*>(format)) return plain->native();
    if (auto* layout = dynamic_cast<D2DTextLayout*>(format)) return layout->native();
    assert(false && "a non-DirectWrite TextFormat reached the Direct2D text backend");
    return nullptr;
}

IDWriteTextLayout* nativeTextLayout(tinta::TextLayout* layout) {
    if (!layout) return nullptr;
    assert(dynamic_cast<D2DTextLayout*>(layout) != nullptr &&
           "a non-DirectWrite TextLayout reached the Direct2D text backend");
    auto* concrete = dynamic_cast<D2DTextLayout*>(layout);
    return concrete ? concrete->native() : nullptr;
}

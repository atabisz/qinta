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
//
// AND THE OFFSETS ALONE DO NOT COVER "WIDENS A FIELD". MEASURED, NOT REASONED.
//
// This block was offsetof-only, and its own comments claimed that was what stopped
// someone tidying a 4-byte BOOL mirror into a 1-byte `bool`. It is not. Every field
// of these four structs is 4 bytes with 4-byte alignment, so narrowing ANY SINGLE
// one of them moves nothing at all: the successor realigns up to where it already
// was, and a trailing field's loss comes straight back as trailing padding.
// Mutating LineMetrics::isTrimmed to `bool` produced zero diagnostics against the
// old block. A standalone probe then showed the same for a MIDDLE field and for the
// second-to-last, so this was never specific to the trailing position -- three
// controls in that probe are what make the reading discriminating rather than a
// probe that sees nothing: narrowing TWO adjacent fields does move offsets
// (sizeof 36 -> 32), and a swapped width/height pair is caught. So the old block
// was live and correctly scoped to REORDERING; it simply had no opinion on width.
//
// Hence the macro: one line per field asserting the offset AND the width, with the
// struct and field named in both messages so a mutation says which field it was.
// Applied to all four mirrors rather than to the one field the mutation happened to
// hit, because the blind spot belongs to the guard, not to isTrimmed.
#define TINTA_ASSERT_FIELD(tintaStruct, nativeStruct, field)                       \
    static_assert(offsetof(tintaStruct, field) == offsetof(nativeStruct, field),    \
                  #tintaStruct "::" #field " is at a different offset than "        \
                  #nativeStruct "::" #field);                                      \
    static_assert(sizeof(tintaStruct::field) == sizeof(nativeStruct::field),        \
                  #tintaStruct "::" #field " is a different width than "            \
                  #nativeStruct "::" #field)

static_assert(sizeof(tinta::TextMetrics) == sizeof(DWRITE_TEXT_METRICS),
              "tinta::TextMetrics must match DWRITE_TEXT_METRICS");
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, left);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, top);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, width);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, widthIncludingTrailingWhitespace);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, height);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, layoutWidth);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, layoutHeight);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, maxBidiReorderingDepth);
TINTA_ASSERT_FIELD(tinta::TextMetrics, DWRITE_TEXT_METRICS, lineCount);

// Same drift alarm for the hit-test struct, and here it is doing more work than
// the TextMetrics one: DWRITE_HIT_TEST_METRICS mixes UINT32, FLOAT and BOOL, so a
// portable struct that spelled the two BOOLs as `bool` would be short by up to 6
// bytes. These assertions are what make `int32_t` in text_api.h a checked decision
// rather than a hopeful one -- and it is the width half of the macro that checks
// it, since neither BOOL sits where narrowing it alone would move an offset.
static_assert(sizeof(tinta::HitTestMetrics) == sizeof(DWRITE_HIT_TEST_METRICS),
              "tinta::HitTestMetrics must match DWRITE_HIT_TEST_METRICS");
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, textPosition);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, length);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, left);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, top);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, width);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, height);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, bidiLevel);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, isText);
TINTA_ASSERT_FIELD(tinta::HitTestMetrics, DWRITE_HIT_TEST_METRICS, isTrimmed);

static_assert(sizeof(tinta::LineMetrics) == sizeof(DWRITE_LINE_METRICS),
              "tinta::LineMetrics must match DWRITE_LINE_METRICS");
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, length);
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, trailingWhitespaceLength);
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, newlineLength);
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, height);
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, baseline);
TINTA_ASSERT_FIELD(tinta::LineMetrics, DWRITE_LINE_METRICS, isTrimmed);

// DWRITE_CLUSTER_METRICS gets sizeof plus TWO fields and not six, because five of
// its members are bit fields and BOTH halves of the macro are ill-formed on one --
// MSVC rejects offsetof with `error C2618: illegal member designator in offsetof`,
// and sizeof on a bit field is ill-formed in the same way. text_api.h carries the
// argument for why that is sufficient rather than a gap: the copy below is by NAME,
// so a bit moving within the bit field cannot corrupt it, and the two things that
// could -- sizeof of the whole struct and the two non-bitfield members -- are
// asserted here.
static_assert(sizeof(tinta::ClusterMetrics) == sizeof(DWRITE_CLUSTER_METRICS),
              "tinta::ClusterMetrics must match DWRITE_CLUSTER_METRICS");
TINTA_ASSERT_FIELD(tinta::ClusterMetrics, DWRITE_CLUSTER_METRICS, width);
TINTA_ASSERT_FIELD(tinta::ClusterMetrics, DWRITE_CLUSTER_METRICS, length);

static_assert(sizeof(tinta::Trimming) == sizeof(DWRITE_TRIMMING),
              "tinta::Trimming must match DWRITE_TRIMMING");
TINTA_ASSERT_FIELD(tinta::Trimming, DWRITE_TRIMMING, granularity);
TINTA_ASSERT_FIELD(tinta::Trimming, DWRITE_TRIMMING, delimiter);
TINTA_ASSERT_FIELD(tinta::Trimming, DWRITE_TRIMMING, delimiterCount);

// tinta::TextRange has default member initializers and DWRITE_TEXT_RANGE does not,
// which costs it trivial default construction but NOT standard layout -- so both
// halves of the macro are still well-defined on it. Worth stating, because "I added
// `= 0` to two fields" is exactly the kind of change that looks like it might have
// invalidated an offsetof and quietly did not.
static_assert(sizeof(tinta::TextRange) == sizeof(DWRITE_TEXT_RANGE),
              "tinta::TextRange must match DWRITE_TEXT_RANGE");
TINTA_ASSERT_FIELD(tinta::TextRange, DWRITE_TEXT_RANGE, startPosition);
TINTA_ASSERT_FIELD(tinta::TextRange, DWRITE_TEXT_RANGE, length);

#undef TINTA_ASSERT_FIELD

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

// --- D2DTextLayout hit testing ----------------------------------------------
//
// The offset contract text_api.h states is an IDENTITY on this backend, and that
// is worth saying out loud rather than leaving as an absent conversion: DirectWrite
// counts text positions in UTF-16 code units, wchar_t is 16 bits here, so a
// caller-buffer index and a DirectWrite text position are the same number. There is
// no mapping code below because there is no mapping to do -- not because it was
// forgotten. The Qt backend is where that contract costs something.
//
// The out-parameter shuffling is real, though. DirectWrite wants BOOL* (4 bytes)
// and FLOAT*, and the seam promises bool* and float*, so each method copies through
// a local rather than casting. `reinterpret_cast<BOOL*>(a bool*)` would compile,
// write 4 bytes into 1, and corrupt whatever the caller put next on the stack.

namespace {

// The struct layout is asserted identical above, so this could be a memcpy. It is
// written field by field for the reason geometry.h gives and the GetMetrics copy
// repeats: a copy needs no aliasing argument to be correct, and if a future SDK
// widens a field the assertions fail loudly while a memcpy would keep compiling.
void copyHitTestMetrics(const DWRITE_HIT_TEST_METRICS& native,
                        tinta::HitTestMetrics* out) {
    out->textPosition = native.textPosition;
    out->length = native.length;
    out->left = native.left;
    out->top = native.top;
    out->width = native.width;
    out->height = native.height;
    out->bidiLevel = native.bidiLevel;
    out->isText = native.isText;
    out->isTrimmed = native.isTrimmed;
}

}  // namespace

bool D2DTextLayout::HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                                 bool* isInside,
                                 tinta::HitTestMetrics* metrics) const {
    // Zero the outputs before the early return, because 5 of the 13 call sites
    // ignore the return value entirely and read the metrics regardless -- e.g.
    // editor.cpp:2512, which uses m.textPosition to place a caret. A refusal that
    // left them untouched would hand those sites uninitialised memory on the one
    // path where the layout is missing.
    if (isTrailingHit) *isTrailingHit = false;
    if (isInside) *isInside = false;
    if (metrics) *metrics = tinta::HitTestMetrics{};
    if (!layout_) return false;

    BOOL nativeTrailing = FALSE;
    BOOL nativeInside = FALSE;
    DWRITE_HIT_TEST_METRICS native{};
    if (FAILED(const_cast<IDWriteTextLayout*>(layout_)->HitTestPoint(
            pointX, pointY, &nativeTrailing, &nativeInside, &native))) {
        return false;
    }

    if (isTrailingHit) *isTrailingHit = nativeTrailing != FALSE;
    if (isInside) *isInside = nativeInside != FALSE;
    if (metrics) copyHitTestMetrics(native, metrics);
    return true;
}

bool D2DTextLayout::HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                                        float* pointX, float* pointY,
                                        tinta::HitTestMetrics* metrics) const {
    if (pointX) *pointX = 0.0f;
    if (pointY) *pointY = 0.0f;
    if (metrics) *metrics = tinta::HitTestMetrics{};
    if (!layout_) return false;

    FLOAT nativeX = 0.0f;
    FLOAT nativeY = 0.0f;
    DWRITE_HIT_TEST_METRICS native{};
    if (FAILED(const_cast<IDWriteTextLayout*>(layout_)->HitTestTextPosition(
            textPosition, isTrailingHit ? TRUE : FALSE, &nativeX, &nativeY,
            &native))) {
        return false;
    }

    if (pointX) *pointX = nativeX;
    if (pointY) *pointY = nativeY;
    if (metrics) copyHitTestMetrics(native, metrics);
    return true;
}

bool D2DTextLayout::HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                                     float originX, float originY,
                                     tinta::HitTestMetrics* metrics,
                                     uint32_t maxMetricsCount,
                                     uint32_t* actualMetricsCount) const {
    // The count-probe pass (metrics == nullptr, maxMetricsCount == 0) is a
    // SUPPORTED call and not a degenerate one: editor.cpp:2792 makes it to size a
    // vector before the real pass. DirectWrite answers it with E_NOT_SUFFICIENT_
    // BUFFER and a filled-in count, so the count must be forwarded even though the
    // HRESULT failed -- returning false here while leaving *actualMetricsCount at 0
    // would make that site conclude the range is empty and draw nothing.
    if (actualMetricsCount) *actualMetricsCount = 0;
    if (!layout_) return false;

    // The one cast in this file, and the asymmetry with copyHitTestMetrics above is
    // deliberate: this parameter is an ARRAY the caller sized, so a field-by-field
    // copy would need a heap temporary of unknown length inside a method that
    // otherwise allocates nothing. The ten assertions at the top of this file --
    // sizeof plus all nine offsets -- are precisely what licenses the cast, which
    // is why they are not ceremony here even though the TextMetrics ones are only a
    // drift alarm.
    UINT32 nativeCount = 0;
    const HRESULT hr = const_cast<IDWriteTextLayout*>(layout_)->HitTestTextRange(
        textPosition, textLength, originX, originY,
        reinterpret_cast<DWRITE_HIT_TEST_METRICS*>(metrics), maxMetricsCount,
        &nativeCount);
    if (actualMetricsCount) *actualMetricsCount = nativeCount;
    return SUCCEEDED(hr);
}

bool D2DTextLayout::GetLineMetrics(tinta::LineMetrics* lineMetrics,
                                  uint32_t maxLineCount,
                                  uint32_t* actualLineCount) const {
    // Same two-pass contract as HitTestTextRange and for the same measured
    // reason: export_html.cpp calls with (nullptr, 0) to size a vector, so the
    // count has to be forwarded even when the HRESULT reports an insufficient
    // buffer. Returning false with the count left at 0 would make that site
    // conclude the layout has no lines and emit no <text> rows at all.
    if (actualLineCount) *actualLineCount = 0;
    if (!layout_) return false;

    // The cast is licensed by the seven assertions at the top of this file --
    // sizeof plus all six offsets -- exactly as HitTestTextRange's is by its ten.
    // The parameter is an array the caller sized, so the alternative is a heap
    // temporary of unknown length in a method that otherwise allocates nothing.
    UINT32 nativeCount = 0;
    const HRESULT hr = const_cast<IDWriteTextLayout*>(layout_)->GetLineMetrics(
        reinterpret_cast<DWRITE_LINE_METRICS*>(lineMetrics), maxLineCount,
        &nativeCount);
    if (actualLineCount) *actualLineCount = nativeCount;
    return SUCCEEDED(hr);
}

bool D2DTextLayout::GetClusterMetrics(tinta::ClusterMetrics* clusterMetrics,
                                      uint32_t maxClusterCount,
                                      uint32_t* actualClusterCount) const {
    if (actualClusterCount) *actualClusterCount = 0;
    if (!layout_) return false;

    // THIS CAST IS LICENSED DIFFERENTLY FROM THE THREE ABOVE IT, and the
    // difference is worth naming because the usual argument does not reach.
    //
    // The other casts rest on offsetof assertions covering every field. Here
    // only sizeof and two of the eight members can be asserted at all --
    // offsetof on a bit field is ill-formed, measured, `error C2618`. What
    // licenses the remaining five is that tinta::ClusterMetrics declares the same
    // bit fields, of the same underlying type, in the same order, and both
    // structs are compiled by ONE implementation in this translation unit. Bit
    // field allocation is implementation-defined but not arbitrary: identical
    // declarations under one implementation get identical layout.
    //
    // That argument is weaker than an assertion, so it is not left to stand
    // alone. The shared cross-backend suite checks isWhitespace on the cluster
    // covering a space, which is a live runtime read THROUGH this cast -- a
    // mismatched bit layout shows up as a wrong flag rather than as nothing.
    UINT32 nativeCount = 0;
    const HRESULT hr = const_cast<IDWriteTextLayout*>(layout_)->GetClusterMetrics(
        reinterpret_cast<DWRITE_CLUSTER_METRICS*>(clusterMetrics), maxClusterCount,
        &nativeCount);
    if (actualClusterCount) *actualClusterCount = nativeCount;
    return SUCCEEDED(hr);
}

// --- D2DTextLayout ranged styling -------------------------------------------
//
// Passthrough, and the range needs no conversion for the reason the hit-test
// section gives at length: the offset contract is an identity on this backend.
// The struct assertions above mean the cast is a checked one, but it is still
// written field by field rather than as a reinterpret_cast -- two fields is not
// worth an aliasing argument.
//
// All three ignore their HRESULT, which is the right shape here rather than a
// shortcut: the seam signature is void because none of the six call sites in the
// tree checks the result today, and DirectWrite's only documented failure for
// these is E_INVALIDARG, which a valid range cannot produce.

void D2DTextLayout::SetFontWeight(tinta::FontWeight weight, tinta::TextRange range) {
    if (!layout_) return;
    const DWRITE_TEXT_RANGE native{range.startPosition, range.length};
    layout_->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(weight), native);
}

void D2DTextLayout::SetFontSize(float fontSize, tinta::TextRange range) {
    if (!layout_) return;
    const DWRITE_TEXT_RANGE native{range.startPosition, range.length};
    layout_->SetFontSize(fontSize, native);
}

void D2DTextLayout::SetStrikethrough(bool strikethrough, tinta::TextRange range) {
    if (!layout_) return;
    const DWRITE_TEXT_RANGE native{range.startPosition, range.length};
    layout_->SetStrikethrough(strikethrough ? TRUE : FALSE, native);
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

#ifndef TINTA_TEXT_D2D_H
#define TINTA_TEXT_D2D_H

// Declaration-only off Windows, with the #error moved to text_d2d.cpp. The
// reasoning is render_d2d.h's, in full there and not repeated: app.h holds 26
// D2DTextFormat and 3 D2DTextFactory members by value, so App cannot be declared
// at all without this header, and no method here touches DirectWrite inline --
// measured, 19 pointer occurrences and 0 by value.
#ifdef _WIN32
#include <windows.h>
#endif

#include "text_api.h"

// dwrite_2.h, not dwrite.h, and THIS HEADER MUST NOT RELY ON WHO INCLUDED IT FIRST.
// IDWriteFontFallback -- which the ApplyFontFallback work put in a member and a
// constructor parameter below -- is declared in dwrite_2.h. It compiled anyway for a
// while, on both platforms, for a reason that was pure luck: app.h forward-declares
// `struct IDWriteFontFallback;` at :62 and includes this header afterwards, so every
// TU that arrives through app.h sees the name. Eleven Windows targets, `tinta` among
// them, do not all arrive that way, and they read 593 errors that all traced to this
// one line. Off Windows the same latent hole exists and is invisible, because the
// forward declarations below are the only definition of these names there and app.h's
// one covered the gap.
//
// So: the real header on Windows, and the missing forward declaration added off it.
// A header that compiles only in a particular include order is a defect that reports
// itself somewhere else.
#ifdef _WIN32
#include <dwrite_2.h>
#else
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteTextLayout;
struct IDWriteInlineObject;
struct IDWriteFontFallback;
// THE SIXTH ARRIVED LATE, AND THE COMPILER IS WHAT SENT IT. This header has named
// IDWriteTextAnalyzer since D2DTextFactory took over App::textAnalyzer (see the member
// at the bottom of this file), but the DECLARATION stayed behind in include/app.h's own
// `#else` and kept working, because app.h forward-declares before it includes this
// header. So this file compiled off Windows only when reached THROUGH app.h.
//
// It surfaced when app.h swept four declarations it measured as having "no live use
// anywhere else in that header" -- true of this name and irrelevant, because the use is
// in a DIFFERENT header that app.h feeds. `scripts/probe-portable-linux.sh` failed on
// this exact line with "'IDWriteTextAnalyzer' does not name a type", which is the whole
// reason that probe is run rather than reasoned about: the Windows build includes the
// real <dwrite.h> and cannot see any of this.
//
// Declared HERE rather than restored there, because the member moved here and its
// declaration should have moved with it.
struct IDWriteTextAnalyzer;
#endif

// The DirectWrite backend for the text seam.
//
// OWNERSHIP HERE IS OWNED, WHICH IS THE OPPOSITE OF render_d2d.h
//
// render_d2d.h's wrappers BORROW, because App already created and already Releases
// the render target and the scratch brush. These wrappers OWN, because the factory
// below is what CREATES the underlying DirectWrite object and is therefore holding
// the only reference to it.
//
// The rule that covers both files, and the only one worth remembering: whoever
// created the COM object owns it. App creates the target and the brush, so those
// wrappers borrow; TextFactory creates formats, layouts and trimming signs, so
// those wrappers Release in their destructors. Getting this backwards in either
// direction is a leak or a double-free, and the asymmetry is not an inconsistency
// -- it is one rule producing different answers from different inputs.
//
// This also means the 73 CreateTextFormat/CreateTextLayout sites lose their
// explicit Release() calls when they are rewired, which is a real part of the
// 271-Release figure render_api.h says a portable backend deletes rather than
// reimplements.
//
// WHY D2DTextLayout DOES NOT DERIVE FROM D2DTextFormat
//
// text_api.h has tinta::TextLayout derive from tinta::TextFormat, mirroring
// DirectWrite and keeping the 35 SetTextAlignment sites from forking by receiver.
// The obvious way to implement that is to have the concrete classes mirror it too
// -- and it is a trap. D2DTextLayout would then inherit tinta::TextFormat twice,
// once through D2DTextFormat and once through tinta::TextLayout, giving two
// distinct base subobjects and making `D2DTextLayout*` to `tinta::TextFormat*`
// ambiguous. Fixing that needs virtual inheritance in the portable header, which
// is a real cost imposed on every backend to save a few lines in one of them.
//
// Instead each class implements the format interface against its own pointer. That
// costs six forwarding calls, and it is not even duplication: an IDWriteTextLayout
// IS an IDWriteTextFormat, so both classes call the same free helpers below on the
// same kind of pointer.

// The six TextFormat operations, against a raw DirectWrite format. Free functions
// rather than a base class precisely so that D2DTextLayout can reuse them without
// inheriting anything -- an IDWriteTextLayout* converts to IDWriteTextFormat* on
// its own, because that is DirectWrite's own inheritance doing the work.
namespace tinta_d2d_text {
void setTextAlignment(IDWriteTextFormat* format, tinta::TextAlignment alignment);
void setParagraphAlignment(IDWriteTextFormat* format, tinta::ParagraphAlignment alignment);
void setWordWrapping(IDWriteTextFormat* format, tinta::WordWrapping wrapping);
void setTrimming(IDWriteTextFormat* format, const tinta::Trimming& trimming,
                 tinta::InlineObject* trimmingSign);
void setLineSpacing(IDWriteTextFormat* format, tinta::LineSpacingMethod method,
                    float lineSpacing, float baseline);
float getFontSize(const IDWriteTextFormat* format);
}  // namespace tinta_d2d_text

class D2DInlineObject final : public tinta::InlineObject {
public:
    explicit D2DInlineObject(IDWriteInlineObject* object) : object_(object) {}
    ~D2DInlineObject() override;

    D2DInlineObject(const D2DInlineObject&) = delete;
    D2DInlineObject& operator=(const D2DInlineObject&) = delete;

    IDWriteInlineObject* native() const { return object_; }

private:
    IDWriteInlineObject* object_;  // owned
};

class D2DTextFormat final : public tinta::TextFormat {
public:
    explicit D2DTextFormat(IDWriteTextFormat* format) : format_(format) {}
    ~D2DTextFormat() override;

    D2DTextFormat(const D2DTextFormat&) = delete;
    D2DTextFormat& operator=(const D2DTextFormat&) = delete;

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override;

    IDWriteTextFormat* native() const { return format_; }

private:
    IDWriteTextFormat* format_;  // owned
};

class D2DTextLayout final : public tinta::TextLayout {
public:
    // The IDWriteFactory is BORROWED and is here for exactly one method:
    // SetTypography, which has to build an IDWriteTypography and DirectWrite offers
    // no way to get a factory back out of an object it made (unlike Direct2D, where
    // every ID2D1Resource has GetFactory). It is a required parameter rather than a
    // defaulted one because a null factory would make SetTypography a silent no-op.
    //
    // TWO CONSTRUCTION SITES: D2DTextFactory::CreateTextLayout in text_d2d.cpp, and
    // src/math_render.cpp's maths-box retain. Count it with `rg 'D2DTextLayout>?\('`
    // rather than trusting this line -- it has already been wrong once, and the two
    // corrections are the useful part of the comment:
    //
    // 2026-09-15, first: it said "exactly ONE construction site", measured by grepping
    // only for make_unique. There were three; the other two used make_shared. MSVC
    // reported both the moment the factory parameter became required. Had the
    // parameter been defaulted to nullptr for convenience, the build would have
    // stayed green and two of the three layouts in the tree would have accepted
    // SetTypography and applied nothing -- the exact silent failure this seam's test
    // exists to make visible, reintroduced one layer below the test. That is the
    // argument for a required parameter: three lines, and it cannot be wrong about
    // how many callers there are.
    //
    // 2026-09-15, same day, second: three became TWO, because render.cpp's site went
    // away when createLayout moved onto app.textFactory. A population under active
    // migration is a moving number, which is the real reason to measure it rather
    // than read it here.
    // The third parameter is a POINTER TO A SLOT, not a fallback pointer, and the
    // indirection is the point: App builds the fallback chain in updateTextFormats,
    // which runs AFTER initD2D has already seated the text factory. A layout that
    // copied the value at construction would capture whatever was there at the time,
    // so ApplyFontFallback would depend on when the layout was made. Reading through a
    // slot reads it at APPLY time, which is exactly when render.cpp reads
    // app.fontFallback today -- so the behaviour is unconditionally the same rather
    // than the same as long as an ordering argument holds.
    //
    // REQUIRED, not defaulted, for the reason the comment above gives at length: a
    // defaulted null here would let a construction site silently produce layouts whose
    // ApplyFontFallback does nothing, which is the failure this seam's tests exist to
    // make visible. Both construction sites pass it explicitly, and math_render.cpp's
    // passes nullptr deliberately -- that layout is a retained maths box and no call
    // site has ever asked it for a fallback.
    D2DTextLayout(IDWriteTextLayout* layout, IDWriteFactory* factory,
                  IDWriteFontFallback* const* fallbackSlot)
        : layout_(layout), factory_(factory), fallbackSlot_(fallbackSlot) {}
    ~D2DTextLayout() override;

    D2DTextLayout(const D2DTextLayout&) = delete;
    D2DTextLayout& operator=(const D2DTextLayout&) = delete;

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override;

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
    void SetCharacterSpacing(float trailingSpacing, tinta::TextRange range) override;
    void SetTypography(const tinta::Typography& typography,
                       tinta::TextRange range) override;
    void ApplyFontFallback() override;

    IDWriteTextLayout* native() const { return layout_; }

private:
    IDWriteTextLayout* layout_;   // owned
    IDWriteFactory* factory_;     // borrowed -- App owns the DWrite factory
    // Borrowed slot, read at apply time -- see the constructor comment. May be null,
    // and a null slot means "this layout has no fallback source", not an error.
    IDWriteFontFallback* const* fallbackSlot_;
};

class D2DTextFactory final : public tinta::TextFactory {
public:
    explicit D2DTextFactory(IDWriteFactory* factory) : factory_(factory) {}
    ~D2DTextFactory() override;

    std::unique_ptr<tinta::TextFormat> CreateTextFormat(
        const wchar_t* fontFamily, tinta::FontWeight weight, tinta::FontStyle style,
        tinta::FontStretch stretch, float fontSize,
        const wchar_t* localeName) override;

    std::unique_ptr<tinta::TextLayout> CreateTextLayout(
        const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
        float maxWidth, float maxHeight) override;

    std::unique_ptr<tinta::InlineObject> CreateEllipsisTrimmingSign(
        tinta::TextFormat* format) override;

    bool AnalyzeLineBreaks(const wchar_t* text, uint32_t length,
                           std::vector<bool>& canBreak) override;

    bool FontFamilyExists(const wchar_t* fontFamily) override;

    // HOLDING THE CHAIN IS NOT APPLYING IT, and that sentence is the whole reason this
    // setter is allowed to exist without reopening the refusal at render.cpp:54-76.
    // Every layout this factory makes now has a route to the chain; only a layout whose
    // caller calls ApplyFontFallback() gets one attached, and exactly two call sites in
    // the tree do -- render.cpp:85 and editor.cpp:92, the second having arrived when
    // createEditorLineLayout's own QI block was retired onto this seam. It was one when
    // this comment was written. The 15 sites the refusal is about call CreateTextLayout and
    // nothing else, so their measured widths cannot move.
    //
    // BORROWED. App owns the chain and releases it in its own teardown; this class must
    // not, or the double release lands in App::releaseNative long after this factory is
    // gone. Called from d2d_init.cpp's updateTextFormats, immediately after the chain is
    // built, and never again -- the chain is built once per process behind
    // `if (!app.fontFallback)`.
    void setFontFallback(IDWriteFontFallback* fallback) { fontFallback_ = fallback; }
    IDWriteFontFallback* fontFallback() const { return fontFallback_; }

private:
    IDWriteFactory* factory_;  // borrowed -- App owns the DWrite factory
    IDWriteFontFallback* fontFallback_ = nullptr;  // borrowed -- App owns it

    // Created on first use rather than in the constructor, and OWNED, which is why
    // this class now has a destructor at all. It replaces App::textAnalyzer, a native
    // member d2d_init.cpp created eagerly on every start whether or not a single
    // non-ASCII paragraph was ever laid out -- the analyzer is only reached from the
    // slow path in analyzeBreakOpportunities, so most sessions never needed it.
    IDWriteTextAnalyzer* analyzer_ = nullptr;
};

// Unwraps a seam handle back to its DirectWrite object, asserting the handle
// really came from this backend. Shared with render_d2d.cpp, which needs both to
// implement DrawText and DrawTextLayout.
IDWriteTextFormat* nativeTextFormat(tinta::TextFormat* format);
IDWriteTextLayout* nativeTextLayout(tinta::TextLayout* layout);

#endif  // TINTA_TEXT_D2D_H

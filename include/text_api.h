#ifndef TINTA_TEXT_API_H
#define TINTA_TEXT_API_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "geometry.h"

// The text seam: font, layout and measurement, with no backend in it.
//
// WHY THIS CUT AND NOT THE ONE I PLANNED
//
// The plan of record was "thin first -- TextFormat plus draw-string only, and add
// metrics later where a call site proves it needs them". Measuring first refuted
// that. Over src+include on 2026-09-11:
//
//   DrawText 106 · GetMetrics 40 · CreateTextFormat 37 · CreateTextLayout 36
//   SetTextAlignment 35 · DrawTextLayout 30 · SetParagraphAlignment 24
//   GetFontSize 24 · SetWordWrapping 19 · SetTrimming 7
//
// GetMetrics is the second-largest text method in the tree, and all 40 of its
// sites are on text LAYOUTS (`layout` 15, `ll` 4, `hint` 4, `tl` 3, and twelve
// more one-off locals -- no App member appears once). DWRITE_TEXT_METRICS is
// named 56 times. In this codebase a layout is not primarily a thing you draw, it
// is a thing you MEASURE and then draw: that is how every wrapped label, chip,
// tooltip and gutter figures out its own box. A layout without GetMetrics would
// have been an object almost nothing could use, so metrics ship here in slice one.
//
// The fork was real, though -- it just falls in a different place than predicted.
// What is genuinely deferrable is HIT-TESTING and PER-RANGE STYLING:
//
//   HitTestTextPosition 6 · GetLineMetrics 5 · SetFontFallback 6 · HitTestTextRange 4
//   SetFontWeight 4 · HitTestPoint 3 · GetClusterMetrics 2 · SetTypography 1
//   SetStrikethrough 1 · SetFontSize 1
//
// 33 sites, concentrated in editor.cpp and render.cpp, neither of which is in
// tests/portable-sources.txt yet. Those also carry a genuine design question this
// header should not answer by accident: a hit-test position is an offset into the
// string, and wchar_t is 16-bit on Windows and 32-bit elsewhere, so an offset is
// UTF-16 code units on one platform and code points on the other. Nothing in the
// tree currently moves such an offset across a platform boundary. The moment
// hit-testing enters this seam, something could, and the answer wants to be
// designed rather than inherited. See the note on string arguments below.
//
// WHAT MEASURING ALSO REMOVED
//
// SetMaxWidth, SetMaxHeight, GetMaxWidth and GetMaxHeight appear NOWHERE in the
// tree -- a layout's box is fixed at creation and never revised. So TextLayout
// carries none of them. That is four methods not written, not four methods
// forgotten, and it is why the layout interface below is five lines long.
//
// Method names stay the Direct2D/DirectWrite ones for the reason render_api.h
// gives at length: it is what makes the port mechanical rather than a 106-site
// hand transcription.
//
// STRING ARGUMENTS
//
// `const wchar_t*` plus a length, exactly as DirectWrite spells it, so all 106
// DrawText sites and their 80 wcslen companions bind unmodified. wchar_t differs
// in width across platforms, which sounds fatal and is not: a pointer and a length
// describing the SAME buffer are self-consistent whatever the element width, and
// the Qt backend converts with QString::fromWCharArray, which handles both 16- and
// 32-bit input. The tree already respects this where it constructs characters --
// math_parser.cpp's codepoint() is guarded on WCHAR_MAX, and the surrogate
// predicates in editor.cpp are simply never true when wchar_t is 32-bit, which is
// the correct answer there rather than a lucky one. What is NOT safe is an integer
// offset into text crossing between platforms, which is the hit-testing note above.

namespace tinta {

// All five enums below take their values from the DWRITE_ enum they mirror, so
// the Windows backend can cast instead of switching and the call sites that
// currently pass DWRITE_FONT_WEIGHT_SEMI_BOLD (20 sites) or
// DWRITE_TEXT_ALIGNMENT_CENTER (17) keep meaning exactly what they meant.
// render_text_d2d.cpp static_asserts every pairing; the assertion cannot live
// here because this header must not include <dwrite.h>.

enum class FontWeight {
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
};

enum class FontStyle {
    Normal = 0,
    Oblique = 1,
    Italic = 2,
};

// Only NORMAL is ever used (37 sites, no other value anywhere), but the enum
// keeps its DirectWrite numbering rather than being a single-value placeholder:
// a backend that receives 5 and a backend that receives 0 disagree about what
// "normal width" means, and one of them would be silently wrong.
enum class FontStretch {
    Normal = 5,
};

enum class TextAlignment {
    Leading = 0,
    Trailing = 1,
    Center = 2,
    Justified = 3,
};

enum class ParagraphAlignment {
    Near = 0,
    Far = 1,
    Center = 2,
};

enum class WordWrapping {
    Wrap = 0,
    NoWrap = 1,
    EmergencyBreak = 2,
    WholeWord = 3,
    Character = 4,
};

enum class TrimmingGranularity {
    None = 0,
    Character = 1,
    Word = 2,
};

enum class LineSpacingMethod {
    Default = 0,
    Uniform = 1,
};

// Two sites pass EnableColorFont (main_d2d.cpp, math_render.cpp) and every other
// DrawText site passes nothing at all. The remaining values are here because the
// parameter is a bit field and a backend must not treat an unrecognised bit as
// zero without saying so.
enum class DrawTextOptions {
    None = 0,
    NoSnap = 1,
    Clip = 2,
    EnableColorFont = 4,
};

// Field-for-field DWRITE_TRIMMING. `delimiter` is a character but is UINT32 in
// DirectWrite, and stays uint32_t here rather than becoming wchar_t: the layout
// claim in the Windows backend depends on the widths matching, and wchar_t would
// break it on exactly the platform where the claim is checked.
struct Trimming {
    TrimmingGranularity granularity;
    uint32_t delimiter;
    uint32_t delimiterCount;
};

// Field-for-field DWRITE_TEXT_METRICS, same names in the same order. Call sites
// read five of the nine -- width 55, height 37, widthIncludingTrailingWhitespace
// 12, left 4, lineCount 2 -- and the other four are carried anyway so that a site
// declaring `DWRITE_TEXT_METRICS m` becomes `tinta::TextMetrics m` as a one-word
// edit with every field access below it untouched.
struct TextMetrics {
    float left;
    float top;
    float width;
    float widthIncludingTrailingWhitespace;
    float height;
    float layoutWidth;
    float layoutHeight;
    uint32_t maxBidiReorderingDepth;
    uint32_t lineCount;
};

// An opaque handle, for the same reason StrokeStyle is one: a trimming sign is
// built once (CreateEllipsisTrimmingSign, 3 sites) and only ever handed back to
// SetTrimming. Three of the seven SetTrimming sites pass one and four pass
// nullptr, so the parameter is real -- but nothing in the tree READS one, which
// is why this has no methods rather than a guessed-at accessor.
class InlineObject {
public:
    virtual ~InlineObject() = default;
};

// Font, size and paragraph-level style: the properties that are the same for the
// whole of a piece of text. Created by TextFactory, then adjusted by the 35
// SetTextAlignment / 24 SetParagraphAlignment / 19 SetWordWrapping sites.
//
// GetFontSize is const and the setters are not, which mirrors how the tree uses
// them: 24 GetFontSize sites are all reads of a long-lived App member
// (editorTextFormat 16, textFormat 3, italicFormat 1) to derive a line height or
// an indent, and none of them wants a non-const handle to do it.
class TextFormat {
public:
    virtual ~TextFormat() = default;

    virtual void SetTextAlignment(TextAlignment alignment) = 0;
    virtual void SetParagraphAlignment(ParagraphAlignment alignment) = 0;
    virtual void SetWordWrapping(WordWrapping wrapping) = 0;
    virtual void SetTrimming(const Trimming& trimming, InlineObject* trimmingSign) = 0;
    virtual void SetLineSpacing(LineSpacingMethod method, float lineSpacing,
                                float baseline) = 0;

    virtual float GetFontSize() const = 0;
};

// Text that has been laid out into a fixed box and can therefore be measured.
//
// Inherits TextFormat because IDWriteTextLayout inherits IDWriteTextFormat, and
// that inheritance is load-bearing rather than decorative: of the 35
// SetTextAlignment sites, some receive formats (app.tocFormat, uiFmt) and some
// receive layouts (layout, fitLayout, btnLayout), and the same is true of
// SetParagraphAlignment, SetWordWrapping and SetTrimming. Splitting the two types
// apart would fork every one of those call sites by receiver.
//
// SetTrimming on a layout is genuinely different from SetTrimming on a format in
// DirectWrite -- one affects a laid-out object, the other a template -- but the
// signature is identical and no call site cares, so it stays one method.
class TextLayout : public TextFormat {
public:
    virtual void GetMetrics(TextMetrics* metrics) const = 0;
};

// Object creation, which render_api.h deliberately left out of the drawing seam
// on the grounds that a backend owns its own construction. Text cannot follow
// that rule: 73 sites create a format or a layout mid-paint from a font family
// and a size, so a portable caller has to be able to ask for one.
//
// Returning unique_ptr rather than an HRESULT plus an out-parameter is the one
// place this seam deliberately does NOT mirror the Direct2D spelling, and it is
// the reason those 73 sites are the only ones this slice will need to rewrite.
// A failed creation returns nullptr; DirectWrite's own failure mode at these
// sites is already a null out-parameter that the caller tests for, so the shape
// callers must handle is unchanged even though the syntax is not.
//
// CreateTextFormat has no font-collection parameter because all 37 sites pass
// nullptr for it. Adding it back is a signature change; leaving it in as a
// permanently-null parameter would be a portable seam carrying a COM concept it
// never uses.
class TextFactory {
public:
    virtual ~TextFactory() = default;

    virtual std::unique_ptr<TextFormat> CreateTextFormat(
        const wchar_t* fontFamily, FontWeight weight, FontStyle style,
        FontStretch stretch, float fontSize, const wchar_t* localeName) = 0;

    virtual std::unique_ptr<TextLayout> CreateTextLayout(
        const wchar_t* string, uint32_t stringLength, TextFormat* format,
        float maxWidth, float maxHeight) = 0;

    virtual std::unique_ptr<InlineObject> CreateEllipsisTrimmingSign(
        TextFormat* format) = 0;
};

}  // namespace tinta

#endif  // TINTA_TEXT_API_H

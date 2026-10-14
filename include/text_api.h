#ifndef TINTA_TEXT_API_H
#define TINTA_TEXT_API_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

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
// CORRECTION: THAT GROUPING WAS A DESCRIPTION, NOT A CENSUS
//
// Written a session later, after checking the coupling against each member. The 33
// are four groups with four different gates, and only the first is gated on the
// offset question:
//
//   hit-testing        13  HitTestTextPosition 6 · HitTestTextRange 4 · HitTestPoint 3
//   per-range styling   7  SetFontWeight 4 · SetTypography 1 · SetStrikethrough 1 · SetFontSize 1
//   pure measurement    7  GetLineMetrics 5 · GetClusterMetrics 2
//   font fallback       6  SetFontFallback
//
// Hit-testing is now on the interface below, with the offset contract stated on
// the methods rather than left open. Of the rest:
//
//   * The 7 MEASUREMENT sites were never gated on anything. GetLineMetrics and
//     GetClusterMetrics are GetMetrics's siblings -- same shape, same direction,
//     no offset in or out -- and they were deferred purely by sitting in the same
//     paragraph as a design question that does not touch them. They need one more
//     struct each and nothing else.
//   * PER-RANGE STYLING needs a TextRange struct, and offsets flow IN only, so the
//     contract above already covers it. SetTypography additionally takes an
//     IDWriteTypography, a COM object nothing in the tree reads.
//     CORRECTED 2026-09-15: that last sentence was a claim about the DirectWrite
//     SPELLING and it was treated as a claim about the population. Measured, the
//     population is two fixed sets of at most three OpenType tags, applied
//     whole-range and never read -- so the portable meaning is a value type, and
//     SetTypography is now on the interface below. See TYPOGRAPHY above. Nothing
//     was wrong with the census; what was wrong was inferring unportability from
//     the shape of the Windows argument rather than from what callers do with it.
//   * SetFontFallback is neither per-range nor styling, and carries no offset at
//     all. It is a whole-layout property behind an IDWriteTextLayout2 QI taking a
//     native IDWriteFontFallback*, and Qt does font fallback automatically, so
//     there is no portable meaning to give it. It stays native deliberately, and
//     createEditorLineLayout keeps its QI by going through nativeTextLayout().
//     STRENGTHENED 2026-09-15: it needs no seam method AT ALL, and the reason is
//     where the calls sit rather than what they do. In render.cpp the QI-and-set is
//     inside createLayout itself, not at any of its 19 call sites, so once creation
//     goes through the factory the whole thing is a backend detail -- the Direct2D
//     backend does the QI, the Qt backend does nothing, and no caller can tell.
//     Six sites that looked like six future seam calls are zero.
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
// the retired Windows backend could cast instead of switching and the call sites that
// then passed DWRITE_FONT_WEIGHT_SEMI_BOLD (20 sites) or
// DWRITE_TEXT_ALIGNMENT_CENTER (17) kept meaning exactly what they meant.
// The retired src/text_d2d.cpp static_asserted the pairings until 0b39ae6; the assertion
// could not live here because this header must not include <dwrite.h>.

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

// THREE sites pass EnableColorFont as of 2026-09-19 -- src/math_render.cpp:733,
// src/paint_document.cpp:491 and tests/render_api_shared.cpp -- and every other DrawText
// site passes nothing at all. The remaining values are here because the parameter is a bit
// field and a backend must not treat an unrecognised bit as zero without saying so.
//
// THIS SAID "two sites (main_d2d.cpp, math_render.cpp)" AND main_d2d.cpp WAS NEVER ONE OF
// THEM: `git log -S EnableColorFont -- src/main_d2d.cpp` is empty across the whole history,
// positive-controlled against math_render.cpp, which returns a7bdb91. So that half was
// false when written rather than gone stale -- the colour-emoji path in main_d2d.cpp went
// through Direct2D's own D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, never this enum, which
// is exactly the confusion that kept four other comments in this tree asserting the seam
// had no colour-emoji option while it had carried one all along.
//
// WHAT THE BACKENDS ACTUALLY DO WITH THE BIT IS NOT UNIFORM, and the seam does not promise
// that it is. Measured 2026-09-19 through tests/render_api_shared.cpp, one black brush and
// one U+1F600, reading max channel spread over the surface:
//
//   direct2d  spread 0 without the bit, 214 with it   -- gates colour on the flag
//   qt        spread 209 either way, surfaces byte-identical -- paints colour glyphs
//             unconditionally and ignores the flag
//
// Both are legal readings of "enable", so the shared test asserts the IMPLICATION rather
// than either shape: with the bit set a colour glyph must appear, and a backend that paints
// neutral without it must change the pixels when it is passed. Do not write "the backends
// honour this flag" here -- one of them has nothing to honour.
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

// Field-for-field DWRITE_TEXT_RANGE, and bound by the OFFSET CONTRACT documented
// on TextLayout below: startPosition and length are indices into the CALLER's
// wchar_t buffer, the same units CreateTextLayout's stringLength already means.
// A backend whose internal representation differs owes the conversion.
//
// Defaulted to the empty range at 0 so that a `TextRange r{}` styles nothing
// rather than silently styling the first character.
struct TextRange {
    uint32_t startPosition = 0;
    uint32_t length = 0;
};

// TYPOGRAPHY -- an OpenType feature list, and the one place this seam is a VALUE
// where DirectWrite has an object.
//
// The note at the top of this header called SetTypography unportable because it
// "takes an IDWriteTypography, a COM object nothing in the tree reads". That was a
// statement about the DirectWrite spelling, not about the population, and measuring
// the population refuted it. The whole of typography in this app is TWO FIXED SETS
// built once and applied whole-range:
//
//   body font  liga · kern · calt
//   code font  liga · dlig
//
// This said "built once in createTypography" until 2026-09-18. That function is
// DELETED -- it lived in the Windows-only src/d2d_init.cpp and had one caller, so the
// Qt stack ran with two empty sets. The builders are now tinta::bodyTypographySet()
// and tinta::codeTypographySet() in include/text_formats.h, App-free and inline so a
// header-only test can call them, seated onto App by installTypography() in
// src/text_formats_app.cpp, which src/qt_init.cpp calls. Do not read the five tags below
// as cosmetic: on the DirectWrite backend, installing no set at all differs from the
// shipped rendering on 85 of 121 printed pages.
//
// Five tags, two sets, never read back, never varied per call site -- all 19 layout
// call sites in render.cpp pass one of exactly two App members. So the portable
// meaning is not an object with a lifetime; it is a short list of four-character
// tags, copied by value like Trimming and TextRange. That is why Typography below
// has no virtual anything and appears in no factory signature.
//
// The tag is held as four ASCII chars rather than DirectWrite's packed uint32,
// because the ASCII form is the one BOTH backends can reconstruct (DirectWrite packs
// it, HarfBuzz and Qt spell it out) and the one that is readable in a test failure.
struct FontFeature {
    char tag[4] = {0, 0, 0, 0};  // e.g. {'l','i','g','a'} -- NOT null-terminated
    uint32_t parameter = 1;      // 1 = enable. Nothing in the tree uses another value.
};

// Fixed capacity, because the population is two sets of at most three. A vector here
// would buy an allocation per layout in a paint path for a list that has never been
// longer than three entries in the history of this file. `add` returns false rather
// than truncating silently if the list is full, so a sixth feature is a visible
// failure rather than a missing ligature nobody can explain.
//
// The tag parameter is `const char (&)[5]` so that add("liga") compiles and
// add("lig") and add("ligax") do not: a wrong-length OpenType tag is the defect this
// type is most likely to be handed, and the compiler can catch all of it.
struct Typography {
    static constexpr size_t kMaxFeatures = 8;

    FontFeature features[kMaxFeatures] = {};
    size_t count = 0;

    bool add(const char (&tag)[5], uint32_t parameter = 1) {
        if (count >= kMaxFeatures) return false;
        FontFeature& f = features[count];
        f.tag[0] = tag[0]; f.tag[1] = tag[1]; f.tag[2] = tag[2]; f.tag[3] = tag[3];
        f.parameter = parameter;
        ++count;
        return true;
    }

    bool has(const char (&tag)[5]) const {
        for (size_t i = 0; i < count; ++i) {
            const FontFeature& f = features[i];
            if (f.tag[0] == tag[0] && f.tag[1] == tag[1] &&
                f.tag[2] == tag[2] && f.tag[3] == tag[3]) return true;
        }
        return false;
    }
};

// DirectWrite's DWRITE_MAKE_OPENTYPE_TAG, spelled portably so that the packing can
// be asserted off Windows and compared against the SDK macro on it. The byte order
// is little-endian-first by OpenType convention, not by accident: 'l' is the LOW
// byte of the tag 'liga'.
inline uint32_t packOpenTypeTag(const FontFeature& feature) {
    return static_cast<uint32_t>(static_cast<unsigned char>(feature.tag[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(feature.tag[1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(feature.tag[2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(feature.tag[3])) << 24);
}

// WHETHER THE TARGET STACK CAN HONOUR A FEATURE -- a documented divergence, not a
// backend implementation detail, which is why it is declared here and not in
// src/text_qt.cpp.
//
// Qt 6.4.2 (what is installed, and what Ubuntu 24.04 ships) has NO per-font feature
// API: QFont::setFeature arrived in 6.7 and is absent from this qfont.h. What Qt can
// still honour, and how:
//
//   kern  QFont::setKerning(true) -- an explicit API, so this one is really applied
//   liga  a HarfBuzz default; on unless a font turns it off. Free.
//   calt  a HarfBuzz default. Free.
//   dlig  NOT EXPRESSIBLE. Dropped.
//
// So four of the five survive and `dlig` on code spans does not. That is Alex's
// call, taken 2026-09-15 over the alternative of requiring Qt 6.7+ for one tag, and the
// reasoning it was taken on was "most programming fonts ship their =>/!= ligatures under
// `calt` or `liga` rather than `dlig`, so the visible loss is probably nil".
//
// HALF OF THAT IS NOW MEASURED, 2026-09-18, off the font files on this host -- and the
// half that failed is the reassuring half. The mechanism is confirmed: Cascadia Code
// 2102.025 exposes no `dlig` at all (nor `liga`) and carries its ligatures on
// `calt`/`rclt`, so on the DEFAULT theme -- index 5, "Midnight", whose codefont IS
// Cascadia Code -- the code set is inert on both backends and the loss really is nil.
// The scope is not: Consolas 7.01 DOES expose `dlig`, and 7 of the 10 built-in themes
// use Consolas, so the divergence is reachable on a majority of themes rather than on
// exotic fonts only. What remains genuinely unmeasured is whether those substitutions
// are ones a reader NOTICES in code -- a font exposing a feature is not the same claim
// as the feature moving a glyph, and no page in this repo has been printed both ways on
// a Consolas theme. So "probably" is still doing work here, just in a narrower sentence,
// and it is still why the divergence is asserted in a test rather than left for a
// screenshot. Alex may want to revisit the call on the 7-of-10 figure; that is his, not
// a thing to change under a comment edit. When the Direct2D backend goes, code spans
// lose discretionary ligatures on Windows too.
//
// Raising the floor to Qt 6.7 would make this function return true for everything
// and is the only reason to revisit it.
inline bool qtCanHonourFeature(const FontFeature& feature) {
    const char* t = feature.tag;
    const bool isKern = t[0] == 'k' && t[1] == 'e' && t[2] == 'r' && t[3] == 'n';
    const bool isLiga = t[0] == 'l' && t[1] == 'i' && t[2] == 'g' && t[3] == 'a';
    const bool isCalt = t[0] == 'c' && t[1] == 'a' && t[2] == 'l' && t[3] == 't';
    return isKern || isLiga || isCalt;
}

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

// Field-for-field DWRITE_HIT_TEST_METRICS, same names in the same order, and
// carried whole for the reason TextMetrics is: a site declaring
// `DWRITE_HIT_TEST_METRICS m` becomes `tinta::HitTestMetrics m` as a one-word
// edit, across all 10 declarations, with every field access below it untouched.
// Call sites read six of the nine -- left 6, width 4, textPosition 3, length 3,
// top 3, height 2 -- and bidiLevel, isText and isTrimmed are read nowhere.
//
// isText and isTrimmed are int32_t and not bool. They are BOOL in DirectWrite,
// which is 4 bytes, and the layout claim in the Windows backend is the only thing
// checking that this struct still matches the SDK's. Same reason
// Trimming::delimiter stays uint32_t rather than wchar_t.
//
// This comment used to end "-- a 1-byte bool would break exactly the assertion
// that exists to catch a widened field". That was false, and measurement is what
// showed it. Every field here is 4 bytes with 4-byte alignment, so narrowing any
// single one moves NO offset and leaves sizeof at 36: the successor realigns up to
// where it already was, and the last member's loss returns as trailing padding.
// Narrowing isTrimmed, isText, or a middle field are all invisible to an
// offsetof-only guard -- confirmed by mutating the same shape in LineMetrics,
// which produced zero diagnostics. What the offsets do catch is REORDERING. The
// backend's assertions now check each field's width alongside its offset, and that
// is what makes int32_t a checked decision.
struct HitTestMetrics {
    uint32_t textPosition;
    uint32_t length;
    float left;
    float top;
    float width;
    float height;
    uint32_t bidiLevel;
    int32_t isText;
    int32_t isTrimmed;
};

// Field-for-field DWRITE_LINE_METRICS, same names in the same order, carried
// whole for the reason TextMetrics is. Counting SITES, not occurrences, call sites
// read four of the six -- baseline 3 (d2d_init, math_render, render), height 2,
// length 1, newlineLength 1 -- and trailingWhitespaceLength and isTrimmed are read
// nowhere. This said "length 2" first time round: the second match was
// render.cpp's `clusters[ci].length`, which is a CLUSTER length and belongs to the
// struct below. A field census taken by pattern rather than by site will pull in
// any struct that happens to share a field name. They are carried
// anyway because the layout assertion in the Windows backend is what makes an
// SDK change loud, and a field omitted from the mirror is a field whose offset
// nothing checks.
//
// isTrimmed is int32_t and not bool, for exactly the reason
// HitTestMetrics::isTrimmed is: BOOL is 4 bytes. This is the field the mutation
// was actually run on -- see the note above for why an offsetof-only guard could
// not see it, and why the backend now asserts widths too.
struct LineMetrics {
    uint32_t length;
    uint32_t trailingWhitespaceLength;
    uint32_t newlineLength;
    float height;
    float baseline;
    int32_t isTrimmed;
};

// Field-for-field DWRITE_CLUSTER_METRICS -- and the one struct in this header
// whose layout claim is WEAKER IN KIND than its neighbours', for a reason worth
// stating rather than discovering later.
//
// Five of the eight members are bit fields, and offsetof on a bit field is
// ill-formed in C++. Measured, not assumed: MSVC rejects
// offsetof(DWRITE_CLUSTER_METRICS, isWhitespace) with
// `error C2618: illegal member designator in offsetof`, while offsetof on width
// and length in the same translation unit compiles. So the Windows backend can
// assert sizeof plus the offsets of width and length, and nothing more.
//
// That is not a gap waiting to be closed. The copy across this seam is
// field-by-field BY NAME, never a memcpy, so a future SDK moving a bit WITHIN
// the bit field cannot corrupt it. What could corrupt it is a change to sizeof
// or to the two byte-offset members, and those are exactly what is asserted.
//
// The bit fields are mirrored AS bit fields rather than widened to bytes, to
// hold sizeof at 8. That matters: render.cpp allocates one of these per cluster
// for a whole text run, so widening them would double a per-character
// allocation to buy an assertion the paragraph above shows is unnecessary.
//
// Call sites read two of the eight -- width 1, length 1 -- and all five flags
// are read nowhere. A backend with no cluster concept of its own should leave a
// flag it cannot answer honestly at zero rather than derive something
// plausible; nothing reads them, so a fabricated canWrapLineAfter would be an
// unasserted semantic dressed up as a feature.
struct ClusterMetrics {
    float width;
    uint16_t length;
    uint16_t canWrapLineAfter : 1;
    uint16_t isWhitespace : 1;
    uint16_t isNewline : 1;
    uint16_t isSoftHyphen : 1;
    uint16_t isRightToLeft : 1;
    uint16_t padding : 11;
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

    // HIT TESTING, AND THE OFFSET CONTRACT
    //
    // **Every text offset and length below -- in either direction -- is an index
    // into the caller's wchar_t buffer, in whatever units that buffer's elements
    // are.** Not UTF-16 code units, not code points: the same units
    // CreateTextLayout's `stringLength` already means. A backend whose internal
    // representation differs owes the conversion, in both directions.
    //
    // The header note above frames this as an open design question. It is not one,
    // and reading the call sites is what settles it -- they had already answered:
    //
    //   selection.cpp:163   off = run->docStart + m.textPosition + (trailing ? m.length : 0)
    //   editor.cpp:321      c   = m.textPosition + (trailing ? m.length : 0)   -> + editorLineStarts[line]
    //   editor.cpp:2517     the same shape, producing a column into editorText
    //
    // All three add the returned offset to a document offset that indexes a
    // std::wstring. An offset in any other unit would be arithmetic between two
    // different coordinate systems -- which compiles, and is silently wrong only
    // for text containing a character outside the BMP, only on a platform where
    // wchar_t is 32-bit, at 13 sites that all look correct. On Windows the rule is
    // an identity, because DirectWrite's units and a 16-bit wchar_t are the same
    // thing, so adopting it cannot change behaviour on the platform that ships.
    //
    // Note that `length` is bound by the same rule as `textPosition`. It is a
    // CLUSTER length in DirectWrite, which sounds internal, but two of the three
    // sites above add it straight to a document offset, so it is caller-facing.
    //
    // The three return bool rather than void -- 8 of the 13 sites branch on
    // SUCCEEDED(), so `SUCCEEDED(x)` becomes `x` and the branch survives the edit.
    // HitTestTextRange follows DirectWrite's two-pass shape (metrics=nullptr,
    // maxMetricsCount=0 to learn the count first), because editor.cpp:2792 uses it.

    virtual bool HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                              bool* isInside, HitTestMetrics* metrics) const = 0;

    virtual bool HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                                     float* pointX, float* pointY,
                                     HitTestMetrics* metrics) const = 0;

    virtual bool HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                                  float originX, float originY,
                                  HitTestMetrics* metrics,
                                  uint32_t maxMetricsCount,
                                  uint32_t* actualMetricsCount) const = 0;

    // MEASUREMENT, AND WHAT A "LINE" HAS TO MEAN
    //
    // Both follow DirectWrite's two-pass shape (metrics=nullptr, max=0 to learn
    // the count first), because both consumers use it: export_html.cpp sizes a
    // vector that way and render.cpp does the same for clusters. Both return
    // bool for the reason the hit-test three do -- the sites branch on
    // SUCCEEDED(), so `SUCCEEDED(x)` becomes `x`.
    //
    // `length`, `trailingWhitespaceLength` and `newlineLength` are bound by the
    // OFFSET CONTRACT above, and this is not a technicality. export_html.cpp
    // computes `visible = lm[i].length - lm[i].newlineLength` and adds
    // `lm[i].length` to a position that indexes a std::wstring; render.cpp adds
    // ClusterMetrics::length to a position compared against text.length(). Both
    // are caller-buffer wchar_t indices, and both are the identity on Windows --
    // which is precisely why a backend converting wrongly is silently wrong.
    //
    // A LINE IS A NEWLINE-DELIMITED LINE, NOT A WRAPPED ROW ONLY. DirectWrite
    // breaks a line at an embedded newline as well as at a wrap, and `length`
    // INCLUDES the newline characters that ended it -- which is what makes
    // `length - newlineLength` the visible text. A backend whose layout engine
    // does not break on newlines owes that break. Qt's does not: measured,
    // QTextLayout gives "ab\ncd" ONE line of length 5 with the same natural
    // width as "abcd", so the newline is neither a break nor a glyph there. That
    // divergence is invisible to every other method on this class except through
    // GetMetrics().lineCount and .height, which is how it survived 155
    // cross-backend assertions unnoticed -- no test had ever contained a
    // newline.
    virtual bool GetLineMetrics(LineMetrics* lineMetrics, uint32_t maxLineCount,
                               uint32_t* actualLineCount) const = 0;

    // Clusters are in text order across the whole layout, spanning lines. The
    // two properties the one consumer actually depends on (render.cpp builds a
    // cumulative-width prefix table from them) are relational, and they are what
    // the shared assertions check rather than any absolute width: the lengths
    // sum to the text length, and the widths sum to the layout's advance.
    virtual bool GetClusterMetrics(ClusterMetrics* clusterMetrics,
                                   uint32_t maxClusterCount,
                                   uint32_t* actualClusterCount) const = 0;

    // RANGED STYLING -- three methods, sized by a census rather than by the
    // phrase "add ranged styling".
    //
    // The whole per-range styling population in this tree is EIGHT calls, and it
    // splits cleanly in two. SIX are simple value setters and are what these
    // three methods cover:
    //
    //   editrail.cpp  SetStrikethrough(TRUE, {0, len})        whole text
    //   overlays.cpp  SetFontWeight(SEMI_BOLD, {0, len})  x2  whole text
    //   render.cpp    SetFontSize(size, {0, len})             whole text
    //   render.cpp    SetFontWeight(SEMI_BOLD, {0, len})      whole text
    //   signals.cpp   SetFontWeight(SEMI_BOLD, {emph})        A REAL SUB-RANGE
    //
    // The other TWO are not value setters and deliberately stay native, each
    // documented at its site: render.cpp's SetTypography takes an
    // IDWriteTypography COM object, and startpage.cpp's SetCharacterSpacing
    // needs an IDWriteTextLayout1 obtained by QueryInterface. Neither is a
    // portable concept yet, and pretending otherwise would put a COM type or a
    // COM version negotiation on this header.
    //
    // CORRECTED 2026-09-15 -- ONE of those two, not both. SetTypography is now
    // here, because the argument above was about the Windows argument's shape and
    // not about the population: two fixed tag sets, applied whole-range, never
    // read. Its portable form is the Typography VALUE at the top of this file, so
    // no COM type reaches this header and the objection is answered rather than
    // waived. SetCharacterSpacing's objection is untouched and still stands --
    // that one really is a COM version negotiation, and it stays native.
    //
    // CORRECTED AGAIN 2026-09-18 -- AND NOW BOTH. SetCharacterSpacing is here, and
    // the reason is that its objection was TRUE ONLY WHILE startpage.cpp WAS A
    // WINDOWS FILE. "It stays native" was free when the whole file around it was
    // native; with startpage.cpp being ported it stops being free and becomes a
    // visible regression -- the "Tinta" wordmark loosens and the two section labels
    // tighten, off Windows only. The QueryInterface that the objection was actually
    // about did NOT move onto this header: it lived in text_d2d.cpp, where COM
    // belonged, until that file was deleted in 0b39ae6, and a version negotiation inside
    // one backend is that backend's business. That is the same distinction SetTypography turned on.
    //
    // AND IT IS NARROWED TO THE POPULATION RATHER THAN MIRRORING DWRITE. The native
    // call takes THREE floats -- leading, trailing, minimumAdvanceWidth -- and Qt
    // has exactly ONE letter-spacing knob. A faithful 3-float portable signature
    // would therefore discard two thirds of its own arguments on the Qt side,
    // silently, which is the failure mode this whole seam is built to avoid. The
    // census says it never has to: every call in the tree is
    // (0.0f, tracking, 0.0f, wholeRange), so the seam takes the ONE value that is
    // ever non-zero and both backends honour it in full. A future caller that needs
    // leading spacing is a signature change, and it should be, because that is the
    // point at which a backend would start dropping something.
    //
    // So the per-range group is now FIVE methods, and the ranged-only contract
    // below covers all five identically.
    //
    // The signatures are RANGED-ONLY even though five of the six callers style
    // the whole text. A whole-layout overload would save those five a `{0, len}`
    // they already write today, at the cost of two spellings per property and a
    // second thing for a backend to implement -- and signals.cpp needs the
    // ranged form regardless, so the ranged form is not optional. The offset
    // machinery a range needs already exists here for hit testing.
    //
    // CONTRACT: the range is in caller-buffer units (see TextRange and the
    // OFFSET CONTRACT above). An empty range styles nothing. A range extending
    // past the end of the text has no observable effect beyond the text -- both
    // backends are asserted on that, because it is the one part of this contract
    // a caller can trip over by arithmetic rather than by intent.
    virtual void SetFontWeight(FontWeight weight, TextRange range) = 0;
    virtual void SetFontSize(float fontSize, TextRange range) = 0;
    virtual void SetStrikethrough(bool strikethrough, TextRange range) = 0;

    // Letter tracking, in DIPs, added after each character's advance -- the
    // `trailingSpacing` of DirectWrite's three, and see the block above for why the
    // other two are absent rather than forgotten. Negative tightens, which is what
    // the start page's wordmark asks for.
    //
    // A DIVERGENCE THAT IS REAL AND IS NOT HIDDEN: DirectWrite adds trailing
    // spacing after every character INCLUDING the last, so a tracked run's measured
    // advance carries one extra tracking value; Qt's absolute letter spacing does
    // the same thing through the shaper, but the two engines round it into their
    // advances differently, so a tracked run's measured width will not agree to the
    // pixel across platforms. Nothing in the tree measures a tracked run -- all
    // three callers draw it -- and qt_entry_tests already refuses to compare Linux
    // and Windows pixel counts to each other for the font-fallback reason above.
    virtual void SetCharacterSpacing(float trailingSpacing, TextRange range) = 0;

    // An EMPTY feature list is a legal argument and must apply nothing, because
    // that is what the null IDWriteTypography the call site can already pass
    // means today -- `if (typography)` at render.cpp:51. Making the empty list a
    // no-op keeps the caller's existing shape rather than asking 19 sites to test
    // for it.
    //
    // A backend that cannot honour a feature drops it SILENTLY and by design; see
    // qtCanHonourFeature. This method reports nothing, because a caller that
    // learned "your ligatures were declined" has nothing it could do about it,
    // and the divergence belongs in a test rather than in a return value.
    virtual void SetTypography(const Typography& typography, TextRange range) = 0;

    // Attach the platform's font-fallback chain -- emoji and CJK -- to THIS layout.
    //
    // THIS METHOD IS NOT THE CHANGE THE REFUSAL AT render.cpp:54-76 DECLINED, and the
    // distinction is the whole reason it is shaped this way. That refusal is about
    // WHICH LAYOUTS get a fallback: folding the chain into CreateTextLayout would hand
    // one to 15 sites that have never had one, several of which MEASURE text, so
    // measured widths and therefore layout would move. That argument is untouched here
    // -- this seam moves the DirectWrite MECHANICS out of render.cpp and leaves the
    // decision exactly where it was. The factory may hold the chain; only a caller that
    // calls this method applies it, and today exactly TWO call sites do: render.cpp:85
    // and editor.cpp:92. It was one when this method shipped, and the second arrived by
    // discharging editor.cpp's own deferral -- createEditorLineLayout had a
    // QueryInterface(IDWriteTextLayout2) block whose stated blocker was that this seam
    // did not exist. It does, so the block became this call. A count in a comment has an
    // expiry and nothing in the tree watches it: re-grep rather than trusting this line.
    //
    // No argument, because the chain is a platform object with no portable spelling and
    // a caller has nothing to say about it beyond "yes". A backend with no chain to
    // apply, or none built yet, does nothing -- which is what render.cpp's
    // `if (!app.fontFallback) return;` already meant.
    //
    // Qt honours this by doing NOTHING, and that is a real implementation rather than a
    // stub: QFontDatabase resolves missing glyphs through its own fallback list during
    // shaping, so there is no chain to attach and no call that would attach one. A
    // divergence worth stating: the two platforms will not pick the same substitute
    // font, which is the same reason qt_entry_tests never compares its Linux and
    // Windows pixel counts to each other.
    virtual void ApplyFontFallback() = 0;
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

    // UAX#14 line-break opportunities, which is the one text question tinta asks that
    // is about the SCRIPT rather than about a font: Chinese prose has no spaces, so a
    // backend that cannot answer it treats an entire sentence as one unbreakable word.
    // It lives on the factory rather than on TextLayout because it needs no font, no
    // size and no box -- DirectWrite answers it from IDWriteTextAnalyzer, a sibling of
    // the layout objects this class already makes, and Qt answers it from
    // QTextBoundaryFinder, which needs nothing but the string.
    //
    // THE CONTRACT, spelled out because two of its three clauses are the kind a
    // backend gets wrong while still compiling and still drawing plausible text:
    //
    //   1. canBreak is indexed so that canBreak[i] == true means "a line may break
    //      BEFORE text[i]", in the CALLER's wchar_t units. It is sized by the caller
    //      and an implementation MUST NOT resize it.
    //   2. An implementation writes indices 1 .. length-1 ONLY. Index 0 and index
    //      length belong to the caller: a break before the first character is not a
    //      break, and the end of the string is always a break. Qt's boundary finder
    //      reports a boundary at both ends, so this clause is a real instruction and
    //      not a description of what falls out naturally.
    //   3. Returning false means "this backend did not analyse the text", NOT "there
    //      are no break opportunities". The caller falls back to space-only breaking,
    //      which is exactly what it already does today when DirectWrite's analyzer is
    //      absent or its call fails -- so the shape a caller must handle is unchanged.
    //      A false return leaves canBreak untouched.
    //
    // Caller units matter on Linux, where wchar_t is UTF-32: one caller unit can be
    // two QString units. QtTextFactory maps them rather than assuming they agree, for
    // the same reason QtTextLayout::ensureIndexMap exists.
    virtual bool AnalyzeLineBreaks(const wchar_t* text, uint32_t length,
                                   std::vector<bool>& canBreak) = 0;

    // Is a font family installed? The second question in this class that is not a
    // DirectWrite method name, and the exception is the same one AnalyzeLineBreaks
    // documents: there is nothing to mirror. DirectWrite answers this with a PAIR of
    // calls -- GetSystemFontCollection then FindFamilyName -- so mirroring would mean
    // exporting IDWriteFontCollection through a portable header to reproduce a
    // two-step dance, and Qt answers it in one static call. It lives on the factory
    // for AnalyzeLineBreaks' reason too: no font object, no size, no box.
    //
    // WHY THE SEAM GREW RATHER THAN THE CALLER GOING NATIVE. src/math_render.cpp asks
    // exactly once, to pick "Cambria Math" when it is installed and the theme font
    // when it is not. Keeping that probe behind TINTA_D2D_BACKEND was the cheaper
    // option and is precedented, but it would leave the Qt backend UNABLE to find a
    // maths font on any platform -- so qinta on Windows would draw equations in
    // Segoe UI while tinta, on the same machine and the same installed fonts, draws
    // them in Cambria Math. A portable question with a portable answer belongs here.
    //
    // THE CONTRACT, and the clause worth stating is the second:
    //
    //   1. true means the family is present under that name. Matching is by family
    //      NAME and is case-insensitive on both backends.
    //   2. false means "not present", NOT "could not tell". Both backends fold a
    //      failed lookup into false, because the caller's only use for the answer is
    //      choosing between this family and a fallback it already has -- and on a
    //      failed probe the fallback is the correct choice anyway. A tri-state would
    //      hand the caller a third case with no distinct action.
    //   3. A null or empty family is false. Asking whether the unnamed family exists
    //      is not a question, and DirectWrite's FindFamilyName on a null string is
    //      not somewhere to find out.
    virtual bool FontFamilyExists(const wchar_t* fontFamily) = 0;
};

}  // namespace tinta

#endif  // TINTA_TEXT_API_H

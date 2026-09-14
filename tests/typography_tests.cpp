// Exercises the typography value types in include/text_api.h -- FontFeature,
// Typography, packOpenTypeTag and qtCanHonourFeature.
//
// WHY THIS TARGET EXISTS
//
// text_api.h refused SetTypography for a stated reason: it "takes an
// IDWriteTypography, a COM object nothing in the tree reads". Measuring the
// population refuted the reason. All of typography in this app is two fixed sets of
// at most three OpenType tags, applied whole-range and never read back, and every
// one of the 19 layout call sites in render.cpp passes one of exactly two App
// members. So the portable form is a value, and these types are it.
//
// THE ONE THING WORTH ASSERTING HARDEST is the divergence. Qt 6.4.2 has no per-font
// feature API (QFont::setFeature is 6.7+), so `dlig` on code spans is DROPPED -- a
// decision taken deliberately over raising the Qt floor for one tag. A dropped
// feature is invisible at runtime: nothing fails, text simply renders with one fewer
// ligature substitution, on a font that may not ship the feature anyway. There is no
// instrument in this repo that could ever notice it. So it is asserted here, by name,
// which turns "we decided to drop it" into something that fails loudly if someone
// later assumes all five features survive.
//
// This file compiles on both platforms and deliberately does NOT link either
// backend: everything under test is a value type or an inline function, which is the
// whole point -- the seam's typography contract can be checked without a device, a
// factory or a window.

#include "text_api.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

#ifdef _WIN32
#include <dwrite.h>
#endif

static int failures = 0;
static int assertions = 0;

static void check(bool ok, const char* what) {
    ++assertions;
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// The two sets built by createTypography (src/d2d_init.cpp). Reproduced here rather
// than called, because that function takes an App& and lives in a Windows-only file.
// If it ever changes, this file is where the change should be noticed -- so the
// contents are asserted below, not just the shapes.
static tinta::Typography bodySet() {
    tinta::Typography t;
    t.add("liga");
    t.add("kern");
    t.add("calt");
    return t;
}

static tinta::Typography codeSet() {
    tinta::Typography t;
    t.add("liga");
    t.add("dlig");
    return t;
}

int main() {
    // --- The value types are values -----------------------------------------
    // Copyable and trivially so: a Typography crosses the seam by value 19 times a
    // paint, and the moment it acquires a non-trivial copy that stops being free.
    check(std::is_trivially_copyable<tinta::FontFeature>::value,
          "FontFeature is trivially copyable");
    check(std::is_trivially_copyable<tinta::Typography>::value,
          "Typography is trivially copyable");
    check(std::is_copy_assignable<tinta::Typography>::value,
          "Typography is copy-assignable, so a backend can record what it was handed");

    // A default-constructed Typography must be EMPTY, because an empty list is the
    // documented no-op and is what replaced the null IDWriteTypography callers could
    // already pass. A defaulted `count` of anything but zero would make
    // `Typography t{}` apply one garbage feature.
    tinta::Typography empty;
    check(empty.count == 0, "a default-constructed Typography is empty");
    check(!empty.has("liga"), "an empty Typography has no features");

    // --- add() ---------------------------------------------------------------
    tinta::Typography t;
    check(t.add("liga"), "add returns true on the first feature");
    check(t.count == 1, "add increments count");
    check(t.has("liga"), "has finds the feature just added");
    check(!t.has("dlig"), "has does not find a feature that was not added");
    // TWO SEPARATE DEFAULTS, and the distinction was found by a control rather than
    // by reading the code. This line was originally one assertion named "the default
    // parameter is 1 (enable)". Planting `uint32_t parameter = 0` on FontFeature to
    // check the assertion could see it made NO test fail -- because `add` assigns
    // f.parameter from its OWN default argument and never reads the member
    // initializer at all. So the assertion measured add's signature while its name
    // claimed the member's default, and the member's default had no instrument on it.
    // Both are asserted now, named for what each one actually reaches.
    check(t.features[0].parameter == 1,
          "add's default ARGUMENT enables the feature (parameter 1)");
    tinta::FontFeature defaulted;
    check(defaulted.parameter == 1,
          "a default-constructed FontFeature's own member initializer is 1 (enable), "
          "which is what a caller filling in a tag by hand gets");

    // The tag is NOT null-terminated, and that is deliberate -- four chars exactly.
    // Asserted per byte because a std::strcmp here would read past the array.
    check(t.features[0].tag[0] == 'l' && t.features[0].tag[1] == 'i' &&
          t.features[0].tag[2] == 'g' && t.features[0].tag[3] == 'a',
          "the four tag bytes are stored in order");
    check(sizeof(t.features[0].tag) == 4,
          "the tag is exactly four bytes, with no room for a terminator");

    t.add("kern", 0);
    check(t.count == 2 && t.features[1].parameter == 0,
          "add carries a parameter of 0, so a feature can be explicitly DISABLED");

    // Capacity: full means refuse, not truncate. A silent truncation would present
    // as one missing ligature, which is unfindable; a false return is checkable.
    tinta::Typography full;
    for (size_t i = 0; i < tinta::Typography::kMaxFeatures; ++i) {
        full.add("liga");
    }
    check(full.count == tinta::Typography::kMaxFeatures, "the list fills to capacity");
    check(!full.add("kern"), "add returns FALSE when full rather than truncating");
    check(full.count == tinta::Typography::kMaxFeatures,
          "a refused add does not change count");

    // Capacity is 8 against a real population of 3. Asserted so that shrinking it to
    // "exactly what we use" is a visible decision rather than a quiet one.
    check(tinta::Typography::kMaxFeatures >= 3,
          "capacity covers the largest real set (three features)");

    // --- packOpenTypeTag ------------------------------------------------------
    // Hand-computed: 'l'=0x6C, 'i'=0x69, 'g'=0x67, 'a'=0x61, low byte first.
    tinta::FontFeature liga;
    liga.tag[0] = 'l'; liga.tag[1] = 'i'; liga.tag[2] = 'g'; liga.tag[3] = 'a';
    check(tinta::packOpenTypeTag(liga) == 0x6167696CU,
          "packOpenTypeTag packs 'liga' low-byte-first");

    // A tag with a high bit set would sign-extend if the chars were not cast through
    // unsigned char first, producing 0xFFFFFF80-style garbage. No real OpenType tag
    // is non-ASCII, so this is not a case the app can reach -- it is asserted because
    // the cast is the kind of thing a later simplification removes.
    tinta::FontFeature high;
    high.tag[0] = static_cast<char>(0x80); high.tag[1] = 0; high.tag[2] = 0; high.tag[3] = 0;
    check(tinta::packOpenTypeTag(high) == 0x00000080U,
          "packOpenTypeTag does not sign-extend a high-bit tag byte");

#ifdef _WIN32
    // The claim the portable spelling exists to make: it equals the SDK macro. This
    // block is Windows-only, so a green run off Windows says nothing about it -- and
    // a static_assert here would be indistinguishable from never having compiled.
    // Hence a runtime check with its own printed line, which the Windows tally counts.
    check(tinta::packOpenTypeTag(liga) ==
              static_cast<uint32_t>(DWRITE_MAKE_OPENTYPE_TAG('l', 'i', 'g', 'a')),
          "packOpenTypeTag equals DWRITE_MAKE_OPENTYPE_TAG for 'liga'");

    tinta::FontFeature dligTag;
    dligTag.tag[0] = 'd'; dligTag.tag[1] = 'l'; dligTag.tag[2] = 'i'; dligTag.tag[3] = 'g';
    check(tinta::packOpenTypeTag(dligTag) ==
              static_cast<uint32_t>(DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES),
          "packOpenTypeTag equals the SDK's named DISCRETIONARY_LIGATURES tag");

    tinta::FontFeature kernTag;
    kernTag.tag[0] = 'k'; kernTag.tag[1] = 'e'; kernTag.tag[2] = 'r'; kernTag.tag[3] = 'n';
    check(tinta::packOpenTypeTag(kernTag) ==
              static_cast<uint32_t>(DWRITE_FONT_FEATURE_TAG_KERNING),
          "packOpenTypeTag equals the SDK's named KERNING tag");
#endif

    // --- qtCanHonourFeature: THE ASSERTED DIVERGENCE --------------------------
    tinta::FontFeature f;
    f.tag[0] = 'k'; f.tag[1] = 'e'; f.tag[2] = 'r'; f.tag[3] = 'n';
    check(tinta::qtCanHonourFeature(f),
          "Qt can honour 'kern' -- QTextCharFormat::setFontKerning, a real API");
    f.tag[0] = 'l'; f.tag[1] = 'i'; f.tag[2] = 'g'; f.tag[3] = 'a';
    check(tinta::qtCanHonourFeature(f), "Qt can honour 'liga' -- a HarfBuzz default");
    f.tag[0] = 'c'; f.tag[1] = 'a'; f.tag[2] = 'l'; f.tag[3] = 't';
    check(tinta::qtCanHonourFeature(f), "Qt can honour 'calt' -- a HarfBuzz default");

    f.tag[0] = 'd'; f.tag[1] = 'l'; f.tag[2] = 'i'; f.tag[3] = 'g';
    check(!tinta::qtCanHonourFeature(f),
          "Qt CANNOT honour 'dlig' on 6.4.2 -- the deliberate divergence, dropped by "
          "decision rather than raising the Qt floor to 6.7 for one tag");

    // A tag nobody uses must also be refused, so the predicate is a whitelist rather
    // than a dlig-shaped special case that would silently pass a future feature.
    f.tag[0] = 's'; f.tag[1] = 'w'; f.tag[2] = 's'; f.tag[3] = 'h';
    check(!tinta::qtCanHonourFeature(f),
          "qtCanHonourFeature is a whitelist -- an unlisted tag ('swsh') is refused");

    // Near-misses: one byte wrong must not pass. This is the failure a byte-compare
    // written with the wrong length would produce.
    f.tag[0] = 'k'; f.tag[1] = 'e'; f.tag[2] = 'r'; f.tag[3] = 'm';
    check(!tinta::qtCanHonourFeature(f), "'kerm' is not 'kern'");
    f.tag[0] = 'l'; f.tag[1] = 'i'; f.tag[2] = 'g'; f.tag[3] = 0;
    check(!tinta::qtCanHonourFeature(f), "a truncated 'lig\\0' is not 'liga'");

    // --- The two real sets ----------------------------------------------------
    const tinta::Typography body = bodySet();
    check(body.count == 3, "the body set is three features");
    check(body.has("liga") && body.has("kern") && body.has("calt"),
          "the body set is liga + kern + calt, matching createTypography");
    int bodyHonoured = 0;
    for (size_t i = 0; i < body.count; ++i) {
        if (tinta::qtCanHonourFeature(body.features[i])) ++bodyHonoured;
    }
    check(bodyHonoured == 3, "Qt honours ALL THREE body features -- body text loses nothing");

    const tinta::Typography code = codeSet();
    check(code.count == 2, "the code set is two features");
    check(code.has("liga") && code.has("dlig"),
          "the code set is liga + dlig, matching createTypography");
    int codeHonoured = 0;
    int codeDropped = 0;
    for (size_t i = 0; i < code.count; ++i) {
        if (tinta::qtCanHonourFeature(code.features[i])) ++codeHonoured;
        else ++codeDropped;
    }
    check(codeHonoured == 1 && codeDropped == 1,
          "of the code set Qt honours exactly one and drops exactly one");

    // Stated as a total so the summary figure in text_api.h ("four of the five
    // survive") has an instrument behind it rather than being prose.
    check(bodyHonoured + codeHonoured == 4,
          "four of the five features across both sets survive on Qt 6.4.2");

    std::printf("typography assertions run: %d, failures: %d\n", assertions, failures);
    std::printf("%s\n", failures == 0 ? "all typography assertions passed"
                                      : "typography assertions FAILED");
    return failures == 0 ? 0 : 1;
}

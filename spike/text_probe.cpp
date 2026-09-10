// Spike: can a portable text stack do what tinta uses DirectWrite for?
//
// This is a throwaway probe, not a port. It answers one question per DirectWrite
// capability the tree actually uses, and each answer carries a control that would
// fail if the library were not really doing the work. A probe that only prints
// "PASS" proves nothing; every check below prints the numbers it decided on.
//
// The mapping under test:
//
//   DirectWrite                                  portable equivalent probed here
//   -------------------------------------------  ------------------------------------
//   IDWriteTextAnalyzer::AnalyzeLineBreakpoints   ICU ubrk (UBRK_LINE, UAX#14)
//   IDWriteTextLayout cluster metrics / hit-test  HarfBuzz glyph infos + positions
//   IDWriteFontFallback / FallbackBuilder         fontconfig charset match
//   IDWriteTextFormat weight/style selection      fontconfig weight/slant match
//   IDWriteTextLayout line metrics                FreeType face metrics
//   (nothing — DWrite does it implicitly)         ICU ubidi  <-- NEW cost, see report
//
// Deliberately NOT probed: rasterisation. Skia/Cairo drawing a glyph run is not
// the risk in this port; shaping, breaking, fallback and metrics are.

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const std::string& name, const std::string& evidence) {
    checks++;
    if (!ok) failures++;
    std::printf("  [%s] %-46s %s\n", ok ? "PASS" : "FAIL", name.c_str(), evidence.c_str());
}

static void section(const char* title) { std::printf("\n== %s ==\n", title); }

// --- UTF-8 -> UTF-16, since ICU works in UChar ---
static std::vector<UChar> toU16(const char* utf8) {
    UErrorCode ec = U_ZERO_ERROR;
    int32_t len = 0;
    u_strFromUTF8(nullptr, 0, &len, utf8, -1, &ec);
    ec = U_ZERO_ERROR;
    std::vector<UChar> out(len + 1);
    u_strFromUTF8(out.data(), len + 1, &len, utf8, -1, &ec);
    out.resize(len);
    return out;
}

// Break opportunities before each index, the same shape as render.cpp's canBreak[].
static std::vector<bool> breakOpportunities(const char* utf8, const char* locale) {
    std::vector<UChar> u = toU16(utf8);
    std::vector<bool> canBreak(u.size() + 1, false);
    UErrorCode ec = U_ZERO_ERROR;
    UBreakIterator* bi = ubrk_open(UBRK_LINE, locale, u.data(), (int32_t)u.size(), &ec);
    if (U_FAILURE(ec)) {
        std::printf("  ubrk_open failed: %s\n", u_errorName(ec));
        return canBreak;
    }
    for (int32_t p = ubrk_first(bi); p != UBRK_DONE; p = ubrk_next(bi)) {
        if (p >= 0 && (size_t)p <= u.size()) canBreak[p] = true;
    }
    ubrk_close(bi);
    return canBreak;
}

static size_t countBreaks(const std::vector<bool>& b) {
    size_t n = 0;
    for (size_t i = 1; i + 1 < b.size(); i++) if (b[i]) n++;   // interior only
    return n;
}

// --- fontconfig: find a font file covering a codepoint ---
struct Matched { std::string family, file; bool ok = false; };

static Matched matchForChar(FcChar32 cp, int weight = FC_WEIGHT_REGULAR, int slant = FC_SLANT_ROMAN) {
    Matched out;
    FcPattern* pat = FcPatternCreate();
    if (cp) {
        FcCharSet* cs = FcCharSetCreate();
        FcCharSetAddChar(cs, cp);
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        FcCharSetDestroy(cs);
    }
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcPatternAddInteger(pat, FC_WEIGHT, weight);
    FcPatternAddInteger(pat, FC_SLANT, slant);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern* m = FcFontMatch(nullptr, pat, &res);
    if (m) {
        FcChar8* s = nullptr;
        if (FcPatternGetString(m, FC_FAMILY, 0, &s) == FcResultMatch) out.family = (const char*)s;
        if (FcPatternGetString(m, FC_FILE, 0, &s) == FcResultMatch) out.file = (const char*)s;
        out.ok = !out.file.empty();
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return out;
}

static Matched matchByFamily(const char* family, int weight, int slant) {
    Matched out;
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddInteger(pat, FC_WEIGHT, weight);
    FcPatternAddInteger(pat, FC_SLANT, slant);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern* m = FcFontMatch(nullptr, pat, &res);
    if (m) {
        FcChar8* s = nullptr;
        if (FcPatternGetString(m, FC_FAMILY, 0, &s) == FcResultMatch) out.family = (const char*)s;
        if (FcPatternGetString(m, FC_FILE, 0, &s) == FcResultMatch) out.file = (const char*)s;
        out.ok = !out.file.empty();
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return out;
}

int main() {
    std::printf("tinta text-stack spike\n");
    std::printf("ICU %s · HarfBuzz %s · FreeType/fontconfig via pkg-config\n",
                U_ICU_VERSION, hb_version_string());

    if (!FcInit()) { std::printf("FATAL: FcInit failed\n"); return 2; }

    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft)) { std::printf("FATAL: FT_Init_FreeType failed\n"); return 2; }

    // ---------------------------------------------------------------- P1
    // Replaces IDWriteTextAnalyzer::AnalyzeLineBreakpoints. render.cpp's comment
    // states the requirement precisely: CJK needs a break between almost every
    // pair of ideographs, but never before closing punctuation like 。，」.
    section("P1  UAX#14 line breaking (ICU ubrk)  <-  AnalyzeLineBreakpoints");
    {
        const char* cjk = "\xE8\xBF\x99\xE6\x98\xAF\xE4\xB8\x80\xE4\xB8\xAA\xE6\xB5\x8B\xE8\xAF\x95"
                          "\xE3\x80\x82\xE5\xAE\x83\xE5\xBA\x94\xE8\xAF\xA5\xE6\x96\xAD\xE8\xA1\x8C";
        // 这是一个测试。它应该断行  — 12 ideographs + one 。 (U+3002)
        std::vector<UChar> u = toU16(cjk);
        std::vector<bool> b = breakOpportunities(cjk, "zh");
        size_t interior = countBreaks(b);
        check(interior >= 8, "CJK: many interior break opportunities",
              "found " + std::to_string(interior) + " in " + std::to_string(u.size()) + " chars");

        // The hard rule: 。is U+3002, class CL — no break BEFORE it.
        size_t idxOfPeriod = 0;
        for (size_t i = 0; i < u.size(); i++) if (u[i] == 0x3002) { idxOfPeriod = i; break; }
        check(idxOfPeriod != 0 && !b[idxOfPeriod], "CJK: no break before U+3002 (closing punct)",
              "index " + std::to_string(idxOfPeriod) + ", canBreak=" +
              (b[idxOfPeriod] ? "true" : "false"));

        // CONTROL: English prose must break at spaces only, not between letters.
        std::vector<bool> e = breakOpportunities("hello wonderful world", "en");
        size_t einterior = countBreaks(e);
        check(einterior == 2, "CONTROL English: breaks only after the 2 spaces",
              "found " + std::to_string(einterior));

        // CONTROL: a naive space-splitter would find 0 breaks in the CJK string.
        // That is the current fallback path in render.cpp when analysis fails.
        size_t spaces = 0;
        for (const char* p = cjk; *p; p++) if (*p == ' ') spaces++;
        check(spaces == 0 && interior >= 8, "CONTROL: space-splitting finds 0 here, ICU finds many",
              "spaces=" + std::to_string(spaces) + " icu=" + std::to_string(interior));
    }

    // ---------------------------------------------------------------- P2
    section("P2  Shaping, advances, cluster map (HarfBuzz)  <-  cluster metrics / hit-testing");
    Matched latin = matchByFamily("DejaVu Sans", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
    {
        check(latin.ok, "primary font resolved", latin.family + " -> " + latin.file);
        if (latin.ok) {
            FT_Face face = nullptr;
            int err = FT_New_Face(ft, latin.file.c_str(), 0, &face);
            check(err == 0, "FT_New_Face on the primary font", "err=" + std::to_string(err));
            if (!err) {
                FT_Set_Char_Size(face, 0, 16 * 64, 96, 96);
                hb_font_t* font = hb_ft_font_create(face, nullptr);
                hb_buffer_t* buf = hb_buffer_create();
                hb_buffer_add_utf8(buf, "Wave", -1, 0, -1);
                hb_buffer_guess_segment_properties(buf);
                hb_shape(font, buf, nullptr, 0);
                unsigned n = 0;
                hb_glyph_info_t* gi = hb_buffer_get_glyph_infos(buf, &n);
                hb_glyph_position_t* gp = hb_buffer_get_glyph_positions(buf, &n);

                std::string adv, clusters;
                bool allReal = n > 0;
                int total = 0;
                for (unsigned i = 0; i < n; i++) {
                    if (gi[i].codepoint == 0) allReal = false;
                    total += gp[i].x_advance;
                    adv += std::to_string(gp[i].x_advance / 64) + " ";
                    clusters += std::to_string(gi[i].cluster) + " ";
                }
                check(n == 4, "4 glyphs for \"Wave\"", "n=" + std::to_string(n));
                check(allReal, "no .notdef — the font really covers it", "glyph ids all nonzero");
                check(total > 0, "advances are real (hit-testing input)",
                      "px: " + adv + "total=" + std::to_string(total / 64));
                check(!clusters.empty(), "cluster map present (maps glyph->char index)",
                      "clusters: " + clusters);

                // CONTROL: ask the Latin font for a CJK codepoint. HarfBuzz must
                // report .notdef rather than silently inventing a glyph — this is
                // precisely the condition that makes font fallback necessary.
                hb_buffer_reset(buf);
                hb_buffer_add_utf8(buf, "\xE4\xB8\xAD", -1, 0, -1);   // 中
                hb_buffer_guess_segment_properties(buf);
                hb_shape(font, buf, nullptr, 0);
                gi = hb_buffer_get_glyph_infos(buf, &n);
                bool notdef = (n == 1 && gi[0].codepoint == 0);
                check(notdef, "CONTROL: U+4E2D in a Latin font is .notdef",
                      "n=" + std::to_string(n) + " gid=" + std::to_string(n ? gi[0].codepoint : 999));

                hb_buffer_destroy(buf);
                hb_font_destroy(font);

                // -------------------------------------------------------- P5
                section("P5  Line metrics (FreeType)  <-  IDWriteTextLayout line metrics");
                double asc = face->size->metrics.ascender / 64.0;
                double desc = face->size->metrics.descender / 64.0;
                double height = face->size->metrics.height / 64.0;
                // `height >= asc - desc` is WRONG and the first run of this probe
                // failed on it: FreeType grid-fits ascender and descender to whole
                // pixels independently, so each can round up while `height` rounds
                // down. DejaVu at 16px/96dpi gave 20 / -6 / 25 — a 1px shortfall
                // that is rounding, not a metrics defect. Tolerance is 2px, and the
                // delta is printed so a real disagreement is still visible.
                double sum = asc - desc;
                double delta = sum - height;
                check(asc > 0 && desc < 0 && height > 0 && delta <= 2.0 && delta >= -2.0,
                      "ascent/descent/line-height at 16px/96dpi",
                      "asc=" + std::to_string((int)asc) + " desc=" + std::to_string((int)desc) +
                      " height=" + std::to_string((int)height) +
                      " (asc+|desc|=" + std::to_string((int)sum) +
                      ", grid-fit delta " + std::to_string((int)delta) + "px)");
                FT_Done_Face(face);
            }
        }
    }

    // ---------------------------------------------------------------- P3
    // Replaces IDWriteFontFallback / CreateFontFallbackBuilder (6 sites).
    section("P3  Font fallback (fontconfig charset match)  <-  IDWriteFontFallback");
    {
        struct Case { FcChar32 cp; const char* label; };
        Case cases[] = {
            {0x4E2D, "U+4E2D CJK 中"},
            {0x0627, "U+0627 Arabic alef"},
            {0x0416, "U+0416 Cyrillic Zhe"},
            {0x2603, "U+2603 snowman"},
        };
        for (const Case& c : cases) {
            Matched m = matchForChar(c.cp);
            bool covered = false;
            std::string detail = m.ok ? (m.family + " (" + m.file + ")") : "no match";
            if (m.ok) {
                FT_Face f = nullptr;
                if (FT_New_Face(ft, m.file.c_str(), 0, &f) == 0) {
                    covered = FT_Get_Char_Index(f, c.cp) != 0;   // verify, don't trust the match
                    detail += covered ? "  glyph present" : "  *** no glyph for the codepoint ***";
                    FT_Done_Face(f);
                }
            }
            check(covered, std::string("fallback covers ") + c.label, detail);
        }
    }

    // ---------------------------------------------------------------- P4
    section("P4  Weight/slant selection (fontconfig)  <-  IDWriteTextFormat");
    {
        Matched reg = matchByFamily("DejaVu Sans", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
        Matched bold = matchByFamily("DejaVu Sans", FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
        Matched ital = matchByFamily("DejaVu Sans", FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
        check(reg.ok && bold.ok && reg.file != bold.file, "bold resolves to a different face file",
              "regular=" + reg.file + "  bold=" + bold.file);
        check(reg.ok && ital.ok && reg.file != ital.file, "italic resolves to a different face file",
              "regular=" + reg.file + "  italic=" + ital.file);
    }

    // ---------------------------------------------------------------- P6
    // NOT a replacement for anything tinta calls: DWrite's IDWriteTextLayout does
    // bidi implicitly, and render.cpp's SetBidiLevel is a no-op stub. So this is
    // work the port ADDS rather than ports. Probed to size it.
    section("P6  Bidi reordering (ICU ubidi)  <-  NEW work, DWrite did this implicitly");
    {
        // "hello ARABIC world" with a real RTL run in the middle
        const char* mixed = "hello \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9 world";
        std::vector<UChar> u = toU16(mixed);
        UErrorCode ec = U_ZERO_ERROR;
        UBiDi* bidi = ubidi_open();
        ubidi_setPara(bidi, u.data(), (int32_t)u.size(), UBIDI_DEFAULT_LTR, nullptr, &ec);
        bool ok = U_SUCCESS(ec);
        int runs = ok ? ubidi_countRuns(bidi, &ec) : -1;
        std::string levels;
        if (ok) {
            const UBiDiLevel* lv = ubidi_getLevels(bidi, &ec);
            if (U_SUCCESS(ec)) for (int32_t i = 0; i < (int32_t)u.size(); i++)
                levels += std::to_string((int)lv[i]);
        }
        check(ok && runs >= 3, "mixed LTR/RTL splits into >=3 directional runs",
              "runs=" + std::to_string(runs) + " levels=" + levels);
        ubidi_close(bidi);

        // CONTROL: pure LTR must be a single run at level 0, or the check above
        // would pass on any string and mean nothing.
        std::vector<UChar> l = toU16("plain english text");
        ec = U_ZERO_ERROR;
        UBiDi* b2 = ubidi_open();
        ubidi_setPara(b2, l.data(), (int32_t)l.size(), UBIDI_DEFAULT_LTR, nullptr, &ec);
        int runs2 = U_SUCCESS(ec) ? ubidi_countRuns(b2, &ec) : -1;
        check(runs2 == 1, "CONTROL: pure LTR is exactly 1 run", "runs=" + std::to_string(runs2));
        ubidi_close(b2);
    }

    FT_Done_FreeType(ft);
    FcFini();

    std::printf("\n== RESULT ==\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

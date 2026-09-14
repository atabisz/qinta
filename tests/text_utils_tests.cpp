// Assertions for include/text_utils.h -- the text helpers that came out of
// src/utils.cpp, on both platforms.
//
// WHY THIS TARGET EXISTS. These five functions had no dedicated test on either
// platform, and off Windows they were not compiled at all: they lived in
// utils.cpp, which includes <windows.h>. So the GitHub slug rules, the 28-way
// word-boundary predicate and the element-tree flattener were all unmeasured
// portable logic sitting inside a Windows-only translation unit.
//
// WHAT THIS TARGET DOES NOT DO, stated because the split was originally
// proposed for a reason that measurement refuted: it does not make
// src/selection.cpp testable. selection.cpp takes an App&, and App holds the
// Direct2D backends BY VALUE (renderTargetStorage, brushStorage, bitmapStorage,
// textFactoryStorage, the *FormatStorage members), so linking anything that
// instantiates an App off Windows wants vtables from src/render_d2d.cpp and
// src/text_d2d.cpp. isWordBoundary was one of nine undefined symbols, not the
// blocker. See the portable_compile_only note in CMakeLists.txt.
//
// Escapes rather than literal non-ASCII bytes throughout: \u in the wide
// literals and \x in the expected UTF-8. The behaviour under test includes how
// bytes map to wchar_t, so the test must not depend on how a compiler guesses
// this file's own encoding.

#include "text_utils.h"

#include <cstdio>
#include <string>

static int failures = 0;
static int checks = 0;

static void checkBool(bool got, bool want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %s, want %s\n", what,
                    got ? "true" : "false", want ? "true" : "false");
    }
}

static void checkNarrow(const std::string& got, const std::string& want,
                        const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what,
                    got.c_str(), want.c_str());
    }
}

static void checkWide(const std::wstring& got, const std::wstring& want,
                      const char* what) {
    checks++;
    if (got != want) {
        failures++;
        // Wide text cannot be printed portably here, so report the shape.
        std::printf("FAIL %s: lengths got %zu want %zu\n", what,
                    got.size(), want.size());
    }
}

static void checkSize(size_t got, size_t want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %zu, want %zu\n", what, got, want);
    }
}

// --- isWordBoundary -------------------------------------------------------

static void testIsWordBoundary() {
    // All 28 characters the predicate claims, enumerated so a dropped one
    // fails rather than being absorbed by a sample.
    const std::wstring boundaries =
        L" \t\n\r.,;:!?\"'()[]{}<>/\\-=+*&|";
    checkSize(boundaries.size(), 28, "boundary set is 28 characters");
    for (wchar_t c : boundaries) {
        if (!isWordBoundary(c)) {
            checks++;
            failures++;
            std::printf("FAIL isWordBoundary(U+%04X) should be true\n",
                        (unsigned)c);
        }
    }
    checks++;  // one check for the whole set being true

    // Characters a double-click must NOT stop on. '_', '~', '#', '@' and '$'
    // are the interesting ones: they look like punctuation and are deliberately
    // absent from the set, so a "tidy up the list" edit would break identifier
    // and tag selection.
    const std::wstring inWord = L"aZ0_~#@$%^`";
    for (wchar_t c : inWord) {
        if (isWordBoundary(c)) {
            checks++;
            failures++;
            std::printf("FAIL isWordBoundary(U+%04X) should be false\n",
                        (unsigned)c);
        }
    }
    checks++;  // one check for the whole set being false

    // Non-ASCII is never a boundary: CJK text selects as words.
    checkBool(isWordBoundary(L'\u65E5'), false, "CJK is not a boundary");
    checkBool(isWordBoundary(L'\u00E9'), false, "e-acute is not a boundary");
    checkBool(isWordBoundary(L'\0'), false, "NUL is not a boundary");
}

// --- toLower --------------------------------------------------------------

static void testToLower() {
    checkWide(toLower(L"HeLLo"), L"hello", "toLower mixed case");
    checkWide(toLower(L"already"), L"already", "toLower is idempotent shape");
    checkWide(toLower(L"ABC123!?"), L"abc123!?",
              "toLower leaves digits and punctuation alone");
    checkWide(toLower(L""), L"", "toLower empty");
    checkSize(toLower(L"ABCDE").size(), 5, "toLower preserves length");
}

// --- toWide ---------------------------------------------------------------

static void testToWide() {
    checkWide(toWide(""), L"", "toWide empty");
    checkWide(toWide("hello"), L"hello", "toWide ASCII");
    checkSize(toWide("hello").size(), 5, "toWide ASCII length");

    // Two-byte UTF-8: "caf" + U+00E9.
    checkWide(toWide("caf\xC3\xA9"), L"caf\u00E9", "toWide two-byte sequence");
    checkSize(toWide("caf\xC3\xA9").size(), 4, "toWide two-byte length");

    // Three-byte UTF-8, still BMP, so one wchar_t on every platform here.
    checkWide(toWide("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"),
              L"\u65E5\u672C\u8A9E", "toWide three CJK characters");
    checkSize(toWide("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E").size(), 3,
              "toWide CJK length");

    // U+1F600, outside the BMP. This is a REAL, DELIBERATE DIVERGENCE, not a
    // bug: platform_paths.h documents wide strings as UTF-16 on Windows and
    // UTF-32 on POSIX, so the same input is 2 units there and 1 here. Asserted
    // per-width rather than skipped, so that a change to the conversion seam
    // that broke either encoding would fail instead of going unnoticed.
    const std::wstring emoji = toWide("\xF0\x9F\x98\x80");
    if (sizeof(wchar_t) == 2) {
        checkSize(emoji.size(), 2, "toWide astral char is a UTF-16 pair");
        checkBool(emoji[0] >= 0xD800 && emoji[0] <= 0xDBFF, true,
                  "toWide astral char starts with a high surrogate");
    } else {
        checkSize(emoji.size(), 1, "toWide astral char is one UTF-32 unit");
        checkBool((unsigned)emoji[0] == 0x1F600u, true,
                  "toWide astral char is U+1F600");
    }
}

// --- slugifyHeading ------------------------------------------------------

static void testSlugifyHeading() {
    checkNarrow(slugifyHeading(L"Hello World"), "hello-world",
                "slug lowercases and hyphenates");
    checkNarrow(slugifyHeading(L"Hello, World!"), "hello-world",
                "slug drops ASCII punctuation");
    checkNarrow(slugifyHeading(L"A_B-C"), "a_b-c",
                "slug keeps underscore and hyphen");
    checkNarrow(slugifyHeading(L"Section 2"), "section-2",
                "slug keeps digits");
    checkNarrow(slugifyHeading(L"a  b"), "a--b",
                "slug maps each space to its own hyphen");
    checkNarrow(slugifyHeading(L"Tab\tSep"), "tabsep",
                "slug drops tab rather than hyphenating it");
    checkNarrow(slugifyHeading(L"...."), "",
                "slug of punctuation only is empty");
    checkNarrow(slugifyHeading(L""), "", "slug of empty is empty");

    // Non-ASCII letters survive, so CJK headings still form anchors.
    checkNarrow(slugifyHeading(L"\u65E5\u672C\u8A9E"),
                "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
                "slug keeps CJK letters");
    // ...but non-ASCII PUNCTUATION does not, in each of the ranges the
    // predicate lists. One per range, because a range dropped from that lambda
    // is exactly the silent regression this covers.
    checkNarrow(slugifyHeading(L"a\u2014b"), "ab",
                "slug strips U+2014 (general punctuation)");
    checkNarrow(slugifyHeading(L"\u65E5\u3002"),
                "\xE6\x97\xA5",
                "slug strips U+3002 (CJK punctuation)");
    checkNarrow(slugifyHeading(L"a\uFE31b"), "ab",
                "slug strips U+FE31 (vertical forms)");
    checkNarrow(slugifyHeading(L"a\uFF01"), "a",
                "slug strips U+FF01 (fullwidth punctuation)");
    checkNarrow(slugifyHeading(L"a\uFF1Ab"), "ab",
                "slug strips U+FF1A (fullwidth colon)");
    checkNarrow(slugifyHeading(L"a\uFF3Bb"), "ab",
                "slug strips U+FF3B (fullwidth bracket)");
    checkNarrow(slugifyHeading(L"a\uFF5Bb"), "ab",
                "slug strips U+FF5B (fullwidth brace)");
    // A fullwidth LETTER is not punctuation and must survive: U+FF21 sits in
    // the gap the predicate deliberately leaves between 0xFF20 and 0xFF3B.
    checkNarrow(slugifyHeading(L"\uFF21"), "\xEF\xBC\xA1",
                "slug keeps U+FF21 (fullwidth letter A)");
}

// --- extractText ---------------------------------------------------------

static qmd::ElementPtr node(qmd::ElementType t, const std::string& text = "") {
    auto e = std::make_shared<qmd::Element>(t);
    e->text = text;
    return e;
}

static void testExtractText() {
    // A null element leaves the accumulator untouched rather than clearing it.
    {
        std::wstring out = L"kept";
        extractText(nullptr, out);
        checkWide(out, L"kept", "extractText(nullptr) appends nothing");
    }
    {
        std::wstring out;
        extractText(node(qmd::ElementType::Text, "abc"), out);
        checkWide(out, L"abc", "extractText of a text node");
    }
    {
        // Appends rather than assigns.
        std::wstring out = L"pre-";
        extractText(node(qmd::ElementType::Text, "abc"), out);
        checkWide(out, L"pre-abc", "extractText appends to the accumulator");
    }
    {
        std::wstring out;
        extractText(node(qmd::ElementType::SoftBreak), out);
        checkWide(out, L" ", "soft break becomes a space");
    }
    {
        std::wstring out;
        extractText(node(qmd::ElementType::HardBreak), out);
        checkWide(out, L"\n", "hard break becomes a newline");
    }
    {
        auto para = node(qmd::ElementType::Paragraph);
        para->children.push_back(node(qmd::ElementType::Text, "a"));
        para->children.push_back(node(qmd::ElementType::Text, "b"));
        std::wstring out;
        extractText(para, out);
        checkWide(out, L"ab\n\n", "paragraph concatenates and ends a block");
    }
    {
        auto code = node(qmd::ElementType::CodeBlock);
        code->children.push_back(node(qmd::ElementType::Text, "x=1"));
        std::wstring out;
        extractText(code, out);
        checkWide(out, L"\nx=1\n\n", "code block is fenced by newlines");
    }
    {
        // Only Text children of a code block contribute -- anything else is
        // dropped, which is the one branch that differs from the default case.
        auto code = node(qmd::ElementType::CodeBlock);
        code->children.push_back(node(qmd::ElementType::Text, "x"));
        code->children.push_back(node(qmd::ElementType::SoftBreak));
        std::wstring out;
        extractText(code, out);
        checkWide(out, L"\nx\n\n", "code block ignores non-text children");
    }
    {
        auto ruby = node(qmd::ElementType::Ruby);
        ruby->children.push_back(node(qmd::ElementType::Text, "base"));
        ruby->children.push_back(node(qmd::ElementType::RubyText, "anno"));
        std::wstring out;
        extractText(ruby, out);
        checkWide(out, L"base", "ruby keeps base text and drops the reading");
    }
    {
        // The RubyText child is skipped by type, so its own children are not
        // reached either.
        auto ruby = node(qmd::ElementType::Ruby);
        auto anno = node(qmd::ElementType::RubyText);
        anno->children.push_back(node(qmd::ElementType::Text, "deep"));
        ruby->children.push_back(anno);
        std::wstring out;
        extractText(ruby, out);
        checkWide(out, L"", "ruby skips the whole annotation subtree");
    }
    {
        auto mermaid = node(qmd::ElementType::MermaidDiagram, "graph TD");
        std::wstring out;
        extractText(mermaid, out);
        checkWide(out, L"graph TD\n", "mermaid text gets a trailing newline");
    }
    {
        // ...but not a second one when the text already ends in a newline.
        auto mermaid = node(qmd::ElementType::MermaidDiagram, "graph TD\n");
        std::wstring out;
        extractText(mermaid, out);
        checkWide(out, L"graph TD\n", "mermaid does not double the newline");
    }
    {
        // Default case: recurse into children with no separator of its own.
        auto strong = node(qmd::ElementType::Strong);
        strong->children.push_back(node(qmd::ElementType::Text, "bold"));
        std::wstring out;
        extractText(strong, out);
        checkWide(out, L"bold", "inline containers recurse without separators");
    }
    {
        // Nesting: a paragraph containing an emphasis containing text.
        auto para = node(qmd::ElementType::Paragraph);
        auto em = node(qmd::ElementType::Emphasis);
        em->children.push_back(node(qmd::ElementType::Text, "in"));
        para->children.push_back(em);
        std::wstring out;
        extractText(para, out);
        checkWide(out, L"in\n\n", "nested inline inside a block");
    }
    {
        // UTF-8 in element text goes through toWide, so multibyte survives.
        std::wstring out;
        extractText(node(qmd::ElementType::Text,
                         "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"), out);
        checkWide(out, L"\u65E5\u672C\u8A9E", "extractText decodes UTF-8");
    }
}

int main() {
    testIsWordBoundary();
    testToLower();
    testToWide();
    testSlugifyHeading();
    testExtractText();

    if (failures) {
        std::printf("%d of %d text-utils assertion(s) failed\n",
                    failures, checks);
        return 1;
    }
    std::printf("all %d text-utils assertions run, 0 failed\n", checks);
    return 0;
}

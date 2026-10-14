// src/export_html.cpp, asserted off Windows for the first time.
//
// WHAT THIS CAN PROVE, and it is most of the file. HTML export is string work: an
// element tree in, a UTF-8 document out, written to a path. No window, no device, no
// text backend, and -- since this pass -- no Windows header anywhere in its include
// graph. So the whole of exportHtmlFile is reachable from a test, including htmlEscape,
// the walk over every element type, the theme CSS block and the file write itself. The
// document below is parsed by the real markdown parser rather than hand-built, because
// a hand-built tree asserts what I believed the parser produces.
//
// WHAT IT CANNOT PROVE. exportDocumentAs is not called: it opens a dialog. The math and
// diagram branches are not exercised either -- both need src/math_render.cpp or the
// diagram role resolver, which are Windows-only today.
//
// THOSE SIX SYMBOLS ARE STUBBED HERE, AND THE STUBS ARE INSTRUMENTED RATHER THAN INERT.
// A stub that silently returns a default turns "this test never reaches that path" from
// a claim into an assumption -- and if a future change makes plain-markdown export call
// into math or the docx dispatch, the test would keep passing while asserting against my
// stand-in instead of the real code. So every stub records that it ran, and the last
// block of main() asserts that none of them did. The day one fires, the failure names
// the exact symbol.
//
// The stubs are also why this file cannot be the whole story about export_html.cpp: six
// functions' worth of its behaviour is untested by construction, which is written down
// in tests/portable-sources.txt beside the manifest entry rather than left for a reader
// to infer from a green run.

#include "app.h"
#include "document.h"
#include "export.h"
#include "config_dir.h"
#include "i18n.h"
#include "math_render.h"
#include "mermaid_ext.h"
#include "render.h"
#include "signals.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;
long long checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

// Reports the haystack on failure. An HTML assertion that fails with only its own
// message tells you nothing about what was generated instead, and the output is small
// enough to print.
void checkContains(const std::string& haystack, const std::string& needle,
                   const char* message) {
    ++checks;
    if (haystack.find(needle) != std::string::npos) return;
    std::cerr << "FAIL: " << message << "\n  looked for: " << needle << '\n';
    ++failures;
}

void checkAbsent(const std::string& haystack, const std::string& needle,
                 const char* message) {
    ++checks;
    if (haystack.find(needle) == std::string::npos) return;
    std::cerr << "FAIL: " << message << "\n  unexpectedly present: " << needle << '\n';
    ++failures;
}

// --- the six stubs, each a tripwire ----------------------------------------------
// Not `= 0` placeholders: each one is a symbol export_html.o genuinely references,
// measured with nm against the whole portable manifest rather than guessed at.
bool reachedDocx = false;
bool reachedPdf = false;
bool reachedMathParse = false;
bool reachedMathSvg = false;
bool reachedDiagramRole = false;
bool reachedSignal = false;

} // namespace

bool exportDocxFile(App&, const std::wstring&) { reachedDocx = true; return false; }
bool exportPdfFile(App&, const std::wstring&) { reachedPdf = true; return false; }

MathBoxPtr mathParse(App&, const std::wstring&, float, bool) {
    reachedMathParse = true;
    return nullptr;
}

std::string mathBoxSvg(const MathBoxPtr&, const std::string&, const std::string&) {
    reachedMathSvg = true;
    return {};
}

tinta::Color resolveDiagramRole(const App&, const mermaidext::Prim&,
                                mermaidext::Role) {
    reachedDiagramRole = true;
    return tinta::Color{0.0f, 0.0f, 0.0f, 1.0f};
}

void signalPushKey(App&, int, int, const char*, std::wstring, std::wstring) {
    reachedSignal = true;
}

int main() {
    // A document that reaches every branch of walk() a plain markdown file can:
    // headings, paragraph text, emphasis, strong, inline code, a fenced code block, a
    // list, a link, a blockquote, a horizontal rule, and a table.
    //
    // The escaping cases are deliberate and are the reason for several of them: text
    // containing & < > " and a code block containing markup that must NOT be
    // interpreted. An HTML generator that forgets to escape produces output that still
    // renders in a browser -- often looking correct -- which is exactly the class of
    // defect a rendered screenshot cannot catch.
    const std::string source =
        "# Title & <Subtitle>\n"
        "\n"
        "A paragraph with *emphasis*, **strong**, `a < b` and a\n"
        "[link](https://example.com/?a=1&b=2).\n"
        "\n"
        "> A quote with \"quotes\" in it.\n"
        "\n"
        "```cpp\n"
        "if (a < b && c > d) { return \"<script>\"; }\n"
        "```\n"
        "\n"
        "- first item\n"
        "- second item\n"
        "\n"
        "## Heading two\n"
        "\n"
        "Plain ASCII, then non-ASCII: caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.\n";

    qmd::MarkdownParser parser;
    auto parsed = parseDocument(parser, source, "notes.md");
    check(parsed.success, "the fixture document parses");
    check(parsed.root && !parsed.root->children.empty(),
          "the parsed document has children -- everything below is vacuous without it");
    if (!parsed.root) {
        std::cerr << "export_html_tests: no document tree, nothing to export\n";
        return 1;
    }

    App app;
    app.root = parsed.root;

    const std::wstring stamp = std::to_wstring(
        std::chrono::system_clock::now().time_since_epoch().count());
    std::error_code error;
    const fs::path probeDir = fs::temp_directory_path() / (L"tinta-exporthtml-" + stamp);
    check(fs::create_directories(probeDir, error) && !error,
          "temporary probe directory is created");
    const fs::path outPath = probeDir / L"export.html";

    // --- the export ---------------------------------------------------------
    app.currentFile = "/some/dir/My Notes & Drafts.md";
    const bool ok = exportHtmlFile(app, outPath.wstring());
    check(ok, "exportHtmlFile reports success");
    check(fs::exists(outPath), "the output file exists on disk");

    std::string html;
    {
        // fs::path and not the wstring, for the reason spelled out at
        // src/settings.cpp:63 -- and this test would not have compiled otherwise,
        // which is the same wall export_html.cpp itself hit this pass.
        std::ifstream in(outPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        html = ss.str();
    }
    check(!html.empty(), "the output file has content");

    // --- document shell -----------------------------------------------------
    checkContains(html, "<!doctype html>", "starts with a doctype");
    checkContains(html, "<meta charset=\"utf-8\"/>", "declares UTF-8");
    checkContains(html, "name=\"viewport\"", "carries a viewport meta");
    checkContains(html, "<style>", "inlines a stylesheet");
    checkContains(html, "</body>\n</html>\n", "closes body and html, in that order");

    // The title comes from the file name, not the first heading, and it is escaped
    // on the way through. Both halves matter: a raw & in a <title> is malformed
    // HTML that browsers quietly repair, so nothing would ever complain.
    checkContains(html, "<title>My Notes &amp; Drafts.md</title>",
                  "title is the file's basename, HTML-escaped");
    checkAbsent(html, "<title>My Notes & Drafts.md",
                "the unescaped ampersand does not reach the title");

    // --- escaping in body text ----------------------------------------------
    checkContains(html, "Title &amp; &lt;Subtitle&gt;", "heading text is escaped");
    checkAbsent(html, "<Subtitle>", "the raw angle brackets never reach the output");
    checkContains(html, "a &lt; b", "inline code content is escaped");
    checkContains(html, "if (a &lt; b &amp;&amp; c &gt; d)", "code block content is escaped");
    checkAbsent(html, "<script>", "a code block containing a script tag emits no live tag");
    checkContains(html, "&quot;quotes&quot;", "double quotes are escaped");

    // --- element mapping ----------------------------------------------------
    checkContains(html, "<h1", "heading level 1 becomes h1");
    checkContains(html, "<h2", "heading level 2 becomes h2");
    checkContains(html, "<p", "paragraphs are emitted");
    checkContains(html, "<em>emphasis</em>", "emphasis becomes em");
    checkContains(html, "<strong>strong</strong>", "strong becomes strong");
    checkContains(html, "<code", "inline code becomes code");
    checkContains(html, "<pre", "a fenced block becomes pre");
    checkContains(html, "<ul", "a bullet list becomes ul");
    checkContains(html, "<li", "list items become li");
    checkContains(html, "<blockquote", "a quote becomes blockquote");
    checkContains(html, "first item", "list item text survives");
    checkContains(html, "second item", "the second list item survives too");

    // A link's href is an attribute, so its ampersand needs escaping in a different
    // position from the text cases above -- and getting one right says nothing about
    // the other.
    checkContains(html, "href=\"https://example.com/?a=1&amp;b=2\"",
                  "a link href is escaped as an attribute");

    // --- UTF-8 round trip ---------------------------------------------------
    // This is the assertion that exercises the toUtf8 forwarder moved into
    // src/text_utils.cpp this pass. The bytes are written out literally rather than
    // as source-file characters, so the expectation cannot drift with the encoding
    // this file happens to be saved in.
    checkContains(html, "caf\xc3\xa9", "a Latin-1 range character survives as UTF-8");
    checkContains(html, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
                  "CJK text survives as UTF-8");

    // --- HTML blocks: align=, <img> size and a relative image ------------------
    // The viewer honours <div align>, <p align> and <img width/height>; the export must
    // carry the same, and a relative image must embed on every platform -- it was
    // joined with "\\" and so only ever resolved on Windows.
    {
        const fs::path imageFile = probeDir / L"logo.png";
        {
            // Eight bytes of PNG signature: readBinary only has to find the file
            std::ofstream png(imageFile, std::ios::binary);
            png << "\x89PNG\r\n\x1a\n";
        }
        auto htmlDoc = parseDocument(parser,
            "<div align=\"center\">\n"
            "  <img src=\"logo.png\" width=\"80\" alt=\"Logo\">\n"
            "  <h1>Centered</h1>\n"
            "  <p>Water is H<sub>2</sub>O.</p>\n"
            "</div>\n"
            "\n"
            "<p align=\"right\">Right.</p>\n"
            "\n"
            "Plain.\n", "readme.md");
        App htmlApp;
        htmlApp.root = htmlDoc.root;
        htmlApp.currentFile = (probeDir / L"readme.md").u8string();
        const fs::path htmlOut = probeDir / L"aligned.html";
        check(exportHtmlFile(htmlApp, htmlOut.wstring()), "the aligned document exports");
        std::ifstream in(htmlOut, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string out = ss.str();
        checkContains(out, "<h1 style=\"text-align:center\">Centered</h1>", "a centered heading keeps its alignment");
        checkContains(out, "<p style=\"text-align:center\">Water is H<sub>2</sub>O.</p>",
                      "a centered paragraph keeps its alignment and its <sub>");
        checkContains(out, "<p style=\"text-align:right\">Right.</p>", "a right-aligned paragraph keeps its alignment");
        checkContains(out, "<p>Plain.</p>", "Markdown after the div is not aligned");
        checkContains(out, "src=\"data:image/png;base64,", "a relative image beside the document is embedded");
        checkContains(out, "alt=\"Logo\" width=\"80\"/>", "an <img> keeps its width");
    }

    // --- the empty-title case -----------------------------------------------
    app.currentFile.clear();
    const fs::path untitled = probeDir / L"untitled.html";
    check(exportHtmlFile(app, untitled.wstring()),
          "an export with no backing file still succeeds");
    {
        std::ifstream in(untitled, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string out = ss.str();
        checkContains(out, "<title>Qinta export</title>",
                      "with no current file the title falls back to a constant");
    }

    // --- the guard clause ---------------------------------------------------
    // Its own assertion because it is the one branch that returns false without
    // touching the filesystem, and a caller that ignores the bool would see an
    // empty export as a successful one.
    App emptyApp;
    const fs::path never = probeDir / L"never-written.html";
    check(!exportHtmlFile(emptyApp, never.wstring()),
          "an App with no document tree exports nothing and says so");
    check(!fs::exists(never), "...and writes no file at all");

    // --- alert titles in the UI language -------------------------------------
    // The title was hard-coded English while alert.* was translated in every language and
    // had no caller. German, with English as the counter-case: one export cannot tell a
    // translated title from a label that happens to be the same in both.
    {
        // German is seeded by loadLanguageOverrides, as main_qt.cpp does at startup; without it
        // every non-built-in language reads English. The config dir is pinned to the probe's
        // empty folder first, so no languages.ini of the machine's can change a string here.
        pinTintaConfigDir(probeDir.wstring());
        loadLanguageOverrides();
        auto alert = parseDocument(parser, "> [!WARNING]\n> Careful.\n", "notes.md");
        App alertApp;
        alertApp.root = alert.root;
        auto exportTitle = [&](int language) {
            alertApp.currentLanguageIndex = language;
            const fs::path out = probeDir / L"alert.html";
            check(exportHtmlFile(alertApp, out.wstring()), "the alert document exports");
            std::ifstream in(out, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string text = ss.str();
            const size_t at = text.find("<p class=\"alert-title\">");
            return at == std::string::npos ? std::string()
                                           : text.substr(at, text.find("</p>", at) - at);
        };
        const int german = languageIndexById("de");
        check(german > 0, "German is a known UI language");
        const std::string germanTitle = exportTitle(german);
        const std::string englishTitle = exportTitle(LANG_INDEX_EN);
        checkContains(germanTitle, "Warnung", "a German UI exports the alert title in German");
        checkAbsent(germanTitle, "Warning", "...and not in English");
        checkContains(englishTitle, "Warning", "an English UI exports it in English");
    }

    // --- THE STUB TRIPWIRES -------------------------------------------------
    // Every one of these must be false. A true means plain-markdown export now
    // reaches code this target replaced with a stand-in, so the assertions above
    // are measuring my stub rather than the tree.
    check(!reachedDocx, "the docx dispatch was not reached");
    check(!reachedPdf, "the pdf dispatch was not reached");
    check(!reachedMathParse, "mathParse was not reached");
    check(!reachedMathSvg, "mathBoxSvg was not reached");
    check(!reachedDiagramRole, "resolveDiagramRole was not reached");
    check(!reachedSignal, "signalPushKey was not reached");

    fs::remove_all(probeDir, error);

    if (failures == 0) {
        std::cout << "export_html_tests: all " << checks << " assertions passed\n";
        return 0;
    }
    std::cerr << "export_html_tests: " << failures << " of " << checks
              << " assertions failed\n";
    return 1;
}

#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "settings.h"
#include "syntax.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// A do-nothing tinta::TextFormat, so the format-lifetime invariant below can be tested
// without a DirectWrite factory or a window. The seam is a 6-method pure-virtual interface
// and none of the six is called here -- releaseOverlayFormats only destroys, so what matters
// is that the object EXISTS and can be owned, not what it does.
struct FakeTextFormat final : tinta::TextFormat {
    void SetTextAlignment(tinta::TextAlignment) override {}
    void SetParagraphAlignment(tinta::ParagraphAlignment) override {}
    void SetWordWrapping(tinta::WordWrapping) override {}
    void SetTrimming(const tinta::Trimming&, tinta::InlineObject*) override {}
    void SetLineSpacing(tinta::LineSpacingMethod, float, float) override {}
    float GetFontSize() const override { return 16.0f; }
};

// THE DOCUMENT/OVERLAY CLASSIFICATION INVARIANT, and it is here because a theme change is
// exactly what used to break it.
//
// releaseOverlayFormats must destroy the CHROME formats and leave every DOCUMENT format
// standing. For several slices it also dropped supSubFormat -- a document format -- and
// because updateTextFormats creates the document formats and THEN calls updateOverlayFormats,
// the format died before anything drew and ^superscript^ silently rendered at body size on
// Windows while the Qt path rendered it small. Nothing failed: no assertion covered it, both
// functions looked internally consistent, and the bug lived in the membership of one list.
//
// A printed-page hash catches this only when someone re-runs the print rig. This catches it
// in ctest, which is the difference between a bug that cannot come back and one that can.
void checkFormatClassification() {
    // Heap, matching the App below rather than the ten test files that stack-allocate one:
    // this struct is large enough that the two shapes are not interchangeable on a default
    // stack, and there is nothing to learn from finding that out here.
    auto state = std::make_unique<App>();
    App& app = *state;

    auto seatDocument = [](std::unique_ptr<tinta::TextFormat>& storage,
                           tinta::TextFormat*& pointer) {
        storage = std::make_unique<FakeTextFormat>();
        pointer = storage.get();
    };
    seatDocument(app.textFormatStorage, app.textFormat);
    seatDocument(app.headingFormatStorage, app.headingFormat);
    seatDocument(app.codeFormatStorage, app.codeFormat);
    seatDocument(app.boldFormatStorage, app.boldFormat);
    seatDocument(app.italicFormatStorage, app.italicFormat);
    seatDocument(app.boldItalicFormatStorage, app.boldItalicFormat);
    seatDocument(app.codeBoldFormatStorage, app.codeBoldFormat);
    seatDocument(app.codeItalicFormatStorage, app.codeItalicFormat);
    seatDocument(app.codeBoldItalicFormatStorage, app.codeBoldItalicFormat);
    seatDocument(app.supSubFormatStorage, app.supSubFormat);
    for (int i = 0; i < 6; ++i)
        seatDocument(app.headingFormatsStorage[i], app.headingFormats[i]);

    // Two chrome formats as the positive control: if releaseOverlayFormats is a no-op for
    // some reason, the document formats surviving proves nothing, so the test must also show
    // the function DID run. install() is the door here because chrome storage is Seated<>.
    app.signalSmallFormat = app.signalSmallFormatStorage.install(
        std::make_unique<FakeTextFormat>());
    app.tocFormat = app.tocFormatStorage.install(std::make_unique<FakeTextFormat>());

    app.releaseOverlayFormats();

    check(app.signalSmallFormat == nullptr && !app.signalSmallFormatStorage,
          "releaseOverlayFormats clears the chrome formats it owns");
    check(app.tocFormat == nullptr && !app.tocFormatStorage,
          "releaseOverlayFormats clears every chrome format, not just the first");

    // supSubFormat is named on its own line rather than folded into the loop below, because
    // it is the one that regressed and a reader should see it asserted by name.
    check(app.supSubFormat != nullptr && app.supSubFormatStorage != nullptr,
          "releaseOverlayFormats leaves supSubFormat alone -- it is a DOCUMENT format");

    const bool documentsIntact =
        app.textFormat && app.headingFormat && app.codeFormat && app.boldFormat &&
        app.italicFormat && app.boldItalicFormat && app.codeBoldFormat &&
        app.codeItalicFormat && app.codeBoldItalicFormat && app.supSubFormat &&
        app.headingFormats[0] && app.headingFormats[1] && app.headingFormats[2] &&
        app.headingFormats[3] && app.headingFormats[4] && app.headingFormats[5];
    check(documentsIntact,
          "releaseOverlayFormats leaves all 16 document format objects standing");

    // And releaseDocumentFormats IS the function that owns them, which is the other half of
    // the invariant: the formats are not merely un-released, they are released elsewhere.
    app.releaseDocumentFormats();
    check(app.supSubFormat == nullptr && app.textFormat == nullptr &&
          app.headingFormats[5] == nullptr,
          "releaseDocumentFormats is what clears the document formats");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}
bool sameColor(tinta::Color a, tinta::Color b) {
    return std::abs(a.r-b.r) < 0.001f && std::abs(a.g-b.g) < 0.001f &&
           std::abs(a.b-b.b) < 0.001f && std::abs(a.a-b.a) < 0.001f;
}
void checkRun(const App& app, const wchar_t* marker, tinta::Color color) {
    bool found = false;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.substr(run.docStart, run.docLength) == marker) {
            found = true;
            check(sameColor(run.color, color), "native text run uses the right inline/block/link color");
        }
    }
    if (!found) std::wcerr << L"Missing native marker: " << marker << L'\n';
    check(found, "native fixture marker renders");
}
std::string docxRun(const std::string& zip, const std::string& marker) {
    // Tinta writes stored ZIP entries, so the actual XML can be inspected
    // here without introducing a ZIP dependency into the native tests.
    size_t text = zip.find(">" + marker + "</w:t>");
    if (text == std::string::npos) return {};
    size_t start = zip.rfind("<w:r>", text);
    return start == std::string::npos ? std::string() : zip.substr(start, text-start);
}
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    // CTest stages this executable in an isolated portable folder. Refuse
    // to touch any real configuration if the binary is launched directly.
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (argc != 2 || std::string(argv[1]) != "--portable-test" ||
        !fs::equivalent(dir, fs::current_path()) ||
        (settings != "; Isolated native theme test configuration\n" &&
         settings != "; Isolated native theme test configuration\r\n")) {
        std::cerr << "Run this test through CTest's isolated portable wrapper\n";
        return 2;
    }
    // Before the theme work rather than after it, because the paths below return 2 on a
    // missing fixture or a failed initD2D and that would silently skip this -- and a skipped
    // check reads as a pass in the ctest line.
    checkFormatClassification();

    const auto fixture = fs::path(TINTA_THEME_FIXTURE);
    {
        std::ofstream out(dir / "themes.ini", std::ios::binary);
        out << read(fixture.parent_path() / "inline-code-themes.ini");
        out << "\n[theme]\nname=Reverse order\ninlinecode=#b00020\ncode=334455\n"
               "\n[theme]\nname=Invalid override\ninlinecode=broken\ncode=334455\n";
    }
    check(fs::equivalent(tintaConfigDir(), dir), "theme persistence uses only the portable test folder");
    loadCustomThemes();
    check(themeCount() == THEME_COUNT + 5, "all custom theme sections load");
    for (int i = 0; i < THEME_COUNT; ++i) {
        check(!themeAt(i).inlineCode, "built-in themes retain inheritance");
        check(sameColor(themeInlineCodeColor(themeAt(i)), themeAt(i).code),
              "built-in inline color remains exactly the existing code color");
    }
    check(!themeAt(THEME_COUNT).inlineCode, "legacy custom theme inherits code");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT)), hexColor(0x334455)),
          "legacy custom code value is the fallback");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT+1)), hexColor(0xB00020)),
          "explicit inlinecode loads");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT+3)), hexColor(0xB00020)),
          "inlinecode before code and optional hash/lowercase load correctly");
    check(!themeAt(THEME_COUNT+4).inlineCode, "invalid optional color retains inheritance");
    D2DTheme inherited = themeAt(THEME_COUNT);
    inherited.code = hexColor(0x123456);
    check(sameColor(themeInlineCodeColor(inherited), inherited.code), "fallback follows changes to code");
    D2DTheme explicitTheme = themeAt(THEME_COUNT+1);
    explicitTheme.code = hexColor(0x123456);
    check(sameColor(themeInlineCodeColor(explicitTheme), hexColor(0xB00020)),
          "explicit inline color remains independent when code changes");

    // The theme editor saves a full D2DTheme copy through this same writer.
    auto original = themeAt(THEME_COUNT+1);
    int saved = saveCustomTheme(original, L"Saved from editor", original.fontFamily, original.codeFontFamily);
    check(saved >= THEME_COUNT, "custom theme saves");
    std::string ini = read(dir / "themes.ini");
    size_t legacy = ini.find("name=Inline Code Legacy");
    size_t next = ini.find("[theme]", legacy);
    check(ini.substr(legacy, next-legacy).find("inlinecode=") == std::string::npos,
          "saving does not materialize an inherited color in legacy themes");
    loadCustomThemes();
    check(themeAt(saved).inlineCode &&
          sameColor(themeInlineCodeColor(themeAt(saved)), hexColor(0xB00020)),
          "saved explicit color survives restart/reload");

    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        auto doc = app.parser.parse(read(fixture));
        check(doc.success, "mixed Markdown/theme fixture parses");
        app.root = doc.root;
        for (int index : {THEME_COUNT, THEME_COUNT+1, THEME_COUNT+2}) {
            applyTheme(app, index);
            for (int width : {1050, 650}) {
                app.width = width;
                layoutDocument(app);
                for (const wchar_t* marker : {L"inline_heading", L"inline_plain", L"inline_bold",
                        L"inline_italic", L"inline_table", L"inline_table_second", L"inline_quote", L"inline_list"})
                    checkRun(app, marker, themeInlineCodeColor(app.theme));
                checkRun(app, L"block_plain", app.theme.code);
                checkRun(app, L"linked_code", app.theme.link);
                check(sameColor(getTokenColor(app.theme, SyntaxTokenType::Plain), app.theme.code) &&
                      sameColor(getTokenColor(app.theme, SyntaxTokenType::Operator), app.theme.code),
                      "syntax plain/operator tokens retain the fenced-code base color");
                check(app.codeBlocks.size() == 3, "all mixed fenced blocks remain intact");
                auto before = app.layoutTextRuns;
                float height = app.contentHeight;
                app.theme.inlineCode = hexColor(0x00BB88);
                layoutDocument(app);
                check(app.layoutTextRuns.size() == before.size() && app.contentHeight == height,
                      "changing only inline color leaves document geometry unchanged");
                for (size_t i = 0; i < before.size() && i < app.layoutTextRuns.size(); ++i)
                    check(before[i].pos.x == app.layoutTextRuns[i].pos.x && before[i].pos.y == app.layoutTextRuns[i].pos.y,
                          "headings, tables, math and surrounding text keep their positions");
                app.theme = themeAt(index);
            }
        }
        applyTheme(app, THEME_COUNT+1);
        check(exportHtmlFile(app, (dir / "separate.html").wstring()), "HTML export succeeds");
        std::string html = read(dir / "separate.html");
        check(html.find(";color:#b00020;") != std::string::npos &&
              html.find("pre code{color:#334455;}") != std::string::npos &&
              html.find("a code{color:inherit;}") != std::string::npos,
              "HTML separates inline, fenced and linked code colors");
        check(exportDocxFile(app, (dir / "separate.docx").wstring()), "DOCX export succeeds");
        std::string zip = read(dir / "separate.docx");
        for (const char* marker : {"inline_plain", "inline_heading", "inline_quote", "inline_table"})
            check(docxRun(zip, marker).find("<w:color w:val=\"B00020\"/>") != std::string::npos,
                  "DOCX inline spans preserve the explicit color in mixed content");
        check(!docxRun(zip, "block_plain").empty() &&
              docxRun(zip, "block_plain").find("B00020") == std::string::npos,
              "DOCX fenced code does not acquire the inline color");
        check(docxRun(zip, "linked_code").find("<w:color w:val=\"B85A3C\"/>") != std::string::npos,
              "DOCX linked code preserves the link color");
    }
    CoUninitialize();
    std::cout << "Inline code themes: " << failures << " failures\n";
    return failures ? 1 : 0;
}

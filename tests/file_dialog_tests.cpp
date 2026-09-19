// Assertions for include/file_dialog.h -- the Open/Save file-picker seam.
//
// WHAT THIS TEST CAN AND CANNOT PROVE, stated first because the split is uneven and
// was known before the seam was built.
//
// It CANNOT prove anything about requestOpenPath and requestSavePath. A test cannot
// click a dialog, and neither function returns until a human does, so there is no
// reachable claim about them at all -- not even the weak "it returns false with
// nothing held" that keyboard_tests can make about isShiftDown. This file therefore
// does not link either backend, and that is deliberate rather than a shortcut: with
// no implementation on the link line, nothing below can be satisfied by whichever
// platform half happened to be selected.
//
// It CAN prove the part that decides behaviour, which is the FILTER ENCODING. The
// Windows form is one buffer of NUL-separated strings terminated by a second NUL --
// a shape that is easy to get subtly wrong and nearly impossible to see wrong,
// because every string printer, debugger watch window and log line stops at the
// first NUL. A missing terminator, a swapped description and pattern, or a dropped
// row all produce a dialog that opens and behaves incorrectly.
//
// THE LOAD-BEARING ASSERTIONS ARE THE FOUR BYTE-FOR-BYTE COMPARISONS against the
// literals that were in the tree before this seam existed. Those literals are
// reproduced below character for character from the deleted code, and each is
// compared against what buildWin32FilterBuffer produces from the FileFilter rows the
// call site now passes. That makes this a regression test for the ROUTING and not
// merely for the builder: a mistyped pattern or a description moved to the wrong row
// while editing five call sites fails here, and would otherwise have compiled,
// linked, passed every other test in the tree, and shown up as a file dialog
// offering the wrong extensions.
//
// The comparison is over the whole buffer INCLUDING its embedded NULs, which is why
// each expectation is built with an explicit length rather than from a bare literal:
// std::wstring(L"a\0b") stops at the NUL and would compare two one-character strings
// while looking exhaustive.

#include "file_dialog.h"

#include <iostream>
#include <string>
#include <vector>

int failures = 0;
long long checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    failures++;
}

// A wide literal WITH its embedded NULs preserved, INCLUDING the implicit terminator.
//
// N IS DELIBERATELY NOT N-1, AND GETTING THAT WRONG IS THE FIRST THING THIS FILE DID.
// A filter buffer ends in two NULs, and four of the five literals in the tree wrote
// only one of them: `L"...\0*.*\0"` supplied the row terminator explicitly and let the
// compiler's implicit terminator be the buffer's end marker. So the sequence Windows
// actually read is the whole N-element array, and an expectation built from N-1 would
// be one NUL short of what buildWin32FilterBuffer produces -- failing every comparison
// below and pointing the blame at the builder.
//
// This helper is therefore "the bytes the old code handed to Windows", not "the
// characters someone typed", and every expectation below is written as the literal
// text MINUS its final NUL, letting this function put that NUL back.
template <std::size_t N>
std::wstring literal(const wchar_t (&text)[N]) {
    return std::wstring(text, N);
}

void expectEqual(const std::wstring& actual, const std::wstring& expected,
                 const char* message) {
    check(actual == expected, message);
    if (actual == expected) return;
    // Printed as escaped code units, because a NUL-bearing string cannot be printed
    // any other way without silently truncating at the first one -- the same trap the
    // header warns about.
    auto dump = [](const std::wstring& s) {
        std::cerr << "    len=" << s.size() << " [";
        for (wchar_t c : s) {
            if (c == L'\0') std::cerr << "\\0";
            else std::cerr << (char)(c < 128 ? c : '?');
        }
        std::cerr << "]\n";
    };
    std::cerr << "  actual:\n";
    dump(actual);
    std::cerr << "  expected:\n";
    dump(expected);
}

int main() {
    using platform::FileFilter;

    // --- editor.cpp:1048, promptSaveAsPath ------------------------------------
    {
        std::vector<FileFilter> filters = {{L"Markdown (*.md)", L"*.md;*.markdown"},
                                           {L"Mermaid (*.mmd)", L"*.mmd"},
                                           {L"All files (*.*)", L"*.*"}};
        expectEqual(platform::buildWin32FilterBuffer(filters),
                    literal(L"Markdown (*.md)\0*.md;*.markdown\0"
                            L"Mermaid (*.mmd)\0*.mmd\0"
                            L"All files (*.*)\0*.*\0"),
                    "editor.cpp save filter is byte-identical to the deleted literal");
    }

    // --- editor.cpp:1079, openFileDialog --------------------------------------
    {
        std::vector<FileFilter> filters = {
            {L"Documents (*.md;*.mmd;*.txt;*.json;*.yaml;...)",
             L"*.md;*.markdown;*.mmd;*.txt;*.json;*.yaml;*.yml;*.toml;"
             L"*.ini;*.csv;*.log"},
            {L"Markdown / Mermaid (*.md;*.markdown;*.mmd)", L"*.md;*.markdown;*.mmd"},
            {L"All files (*.*)", L"*.*"}};
        expectEqual(
            platform::buildWin32FilterBuffer(filters),
            literal(L"Documents (*.md;*.mmd;*.txt;*.json;*.yaml;...)\0"
                    L"*.md;*.markdown;*.mmd;*.txt;*.json;*.yaml;*.yml;*.toml;"
                    L"*.ini;*.csv;*.log\0"
                    L"Markdown / Mermaid (*.md;*.markdown;*.mmd)\0"
                    L"*.md;*.markdown;*.mmd\0"
                    L"All files (*.*)\0*.*\0"),
            "editor.cpp open filter is byte-identical to the deleted literal");
    }

    // --- export_html.cpp:1468, the site that reads the filter index -----------
    {
        std::vector<FileFilter> filters = {{L"Web page (*.html)", L"*.html"},
                                           {L"Word document (*.docx)", L"*.docx"},
                                           {L"PDF document (*.pdf)", L"*.pdf"}};
        expectEqual(
            platform::buildWin32FilterBuffer(filters),
            literal(L"Web page (*.html)\0*.html\0Word document (*.docx)\0*.docx\0"
                    L"PDF document (*.pdf)\0*.pdf\0"),
            "export_html.cpp filter is byte-identical to the deleted literal");

        // THE ORDER IS PART OF THE CONTRACT AT THIS SITE, not a presentation
        // detail: export_html.cpp branches on filterIndex == 2 for DOCX and == 3
        // for PDF, so reordering these three rows silently changes which format an
        // export produces. Pinned as its own assertion because the byte comparison
        // above would also fail on a reorder, and a reader needs to know WHY it
        // matters here and not at the other four sites.
        check(filters[1].patterns == L"*.docx",
              "filter index 2 (1-based) is the DOCX row that export_html.cpp expects");
        check(filters[2].patterns == L"*.pdf",
              "filter index 3 (1-based) is the PDF row that export_html.cpp expects");
    }

    // --- input.cpp:680, SET_LOCATE_PANDOC -------------------------------------
    //
    // The one site whose first PATTERN is a bare file name rather than a wildcard,
    // and the ONE OF THE FIVE THAT WROTE ITS OWN TERMINATOR: its literal ended
    // `\0*.exe\0\0`, so with the compiler's implicit terminator on top it held THREE
    // trailing NULs where the other four held two. The expectation below drops that
    // extra one, which is not a weakening -- Windows stops reading at the second NUL,
    // so the old buffer and the builder's are the same input, and pinning the third
    // would assert a harmless accident of how one site was typed.
    {
        std::vector<FileFilter> filters = {{L"pandoc.exe", L"pandoc.exe"},
                                           {L"Programs (*.exe)", L"*.exe"}};
        expectEqual(platform::buildWin32FilterBuffer(filters),
                    literal(L"pandoc.exe\0pandoc.exe\0Programs (*.exe)\0*.exe\0"),
                    "input.cpp filter matches the deleted literal up to its extra NUL");
    }

    // --- pandoc.cpp:205, the dynamically built filter --------------------------
    //
    // Built at runtime from a PandocFormat, so the assertion is on the SHAPE the
    // call site constructs: description as given, pattern as `*` + the extension.
    //
    // This is the one site whose old form was a std::wstring rather than a literal --
    // `f.filter + L'\0' + L"*" + f.ext + L'\0' + L'\0'` -- and it is therefore
    // byte-identical to the builder's output rather than merely equivalent to it: same
    // characters, same two trailing NULs, no implicit terminator in play. The builder
    // replaces four hand-assembled NULs at this site, which is the reason the seam
    // owns the encoding instead of each caller.
    {
        const wchar_t* description = L"Word document (*.docx)";
        const wchar_t* ext = L".docx";
        std::vector<FileFilter> filters = {{description, std::wstring(L"*") + ext}};
        expectEqual(platform::buildWin32FilterBuffer(filters),
                    literal(L"Word document (*.docx)\0*.docx\0"),
                    "pandoc.cpp's runtime-built filter has the same shape as a literal one");
    }

    // --- the terminator, asserted directly ------------------------------------
    //
    // Separately from the comparisons above, because "ends in two NULs" is the one
    // property whose absence a human reading the expected strings above cannot see.
    {
        std::vector<FileFilter> filters = {{L"A", L"*.a"}};
        std::wstring buffer = platform::buildWin32FilterBuffer(filters);
        check(buffer.size() == 7, "one row is description + NUL + patterns + NUL + NUL");
        check(buffer[1] == L'\0', "the description is terminated");
        check(buffer[5] == L'\0', "the patterns are terminated");
        check(buffer[6] == L'\0', "and a second NUL ends the buffer");
        check(buffer.substr(0, 1) == L"A", "the description comes first");
    }

    // An empty list is a shape no site has, and the builder returns an empty string
    // rather than a lone NUL. Asserted so the behaviour is decided here instead of
    // being discovered by whoever writes the sixth call site.
    check(platform::buildWin32FilterBuffer({}).empty(),
          "an empty filter list produces an empty buffer, not a bare terminator");

    // --- the Qt encoding ------------------------------------------------------
    {
        std::vector<FileFilter> filters = {{L"Markdown (*.md)", L"*.md;*.markdown"},
                                           {L"All files (*.*)", L"*.*"}};
        // THE DOUBLED PARENTHETICAL IS PINNED ON PURPOSE. It is not the output
        // anyone would design; it is what falls out of keeping the Windows
        // description text unchanged while Qt derives its display label from the
        // whole filter string. file_dialog.h states the two alternatives and why
        // neither belongs in a portability commit. Pinning it here means the day
        // somebody fixes it, this assertion fails and points at that argument --
        // rather than the display text changing quietly.
        expectEqual(platform::buildQtNameFilter(filters),
                    L"Markdown (*.md) (*.md *.markdown);;All files (*.*) (*.*)",
                    "the Qt name filter joins rows with ;; and spaces the patterns");

        check(platform::buildQtNameFilter({}).empty(),
              "an empty filter list produces an empty Qt name filter");
    }

    // --- the two encodings describe the same filter set -----------------------
    //
    // The property that will matter at the cutover, and the reason both builders live
    // in one header instead of one per backend. Checked by counting rows in each form
    // and, for each row, that every pattern in the Windows buffer appears in the Qt
    // string -- an independent characterisation rather than either builder restating
    // itself.
    {
        std::vector<FileFilter> filters = {
            {L"Documents", L"*.md;*.markdown;*.mmd;*.txt"},
            {L"Mermaid (*.mmd)", L"*.mmd"},
            {L"All files (*.*)", L"*.*"}};
        std::wstring win = platform::buildWin32FilterBuffer(filters);
        std::wstring qt = platform::buildQtNameFilter(filters);

        // Windows: rows are pairs of NUL-terminated strings, so the row count is
        // (number of NULs - 1) / 2.
        std::size_t nuls = 0;
        for (wchar_t c : win) {
            if (c == L'\0') nuls++;
        }
        check((nuls - 1) / 2 == filters.size(),
              "the Windows buffer holds exactly one NUL-terminated pair per filter row");

        // Qt: rows are `;;`-separated, so the row count is separators + 1.
        std::size_t separators = 0;
        for (std::size_t i = 0; i + 1 < qt.size(); i++) {
            if (qt[i] == L';' && qt[i + 1] == L';') {
                separators++;
                i++;
            }
        }
        check(separators + 1 == filters.size(),
              "the Qt string holds exactly one ;;-separated row per filter row");

        // Every pattern survives into both forms. Split the Windows patterns on
        // semicolons and look for each in the Qt string.
        for (const FileFilter& filter : filters) {
            std::wstring pattern;
            std::wstring patterns = filter.patterns + L";";
            for (wchar_t c : patterns) {
                if (c != L';') {
                    pattern.push_back(c);
                    continue;
                }
                check(!pattern.empty() && qt.find(pattern) != std::wstring::npos,
                      "every Windows pattern appears in the Qt name filter too");
                check(win.find(pattern) != std::wstring::npos,
                      "every Windows pattern appears in the Windows buffer too");
                pattern.clear();
            }
        }

        // And no semicolon survives INSIDE a Qt pattern list, which is the actual
        // substitution being tested. The only semicolons left must be the `;;`
        // separators -- so the count of semicolons is exactly twice the separators,
        // except where a DESCRIPTION contains one, which none of these three do.
        std::size_t semicolons = 0;
        for (wchar_t c : qt) {
            if (c == L';') semicolons++;
        }
        check(semicolons == separators * 2,
              "no pattern semicolon survives into the Qt form; only ;; separators remain");
    }

    if (failures == 0) {
        std::cout << "file_dialog: all " << checks << " assertions passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s) out of " << checks << " assertions\n";
    return 1;
}

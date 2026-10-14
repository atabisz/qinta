#ifndef TINTA_FILE_DIALOG_H
#define TINTA_FILE_DIALOG_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "win32_types.h"

// THE OPEN AND SAVE FILE PICKERS, AT A SEAM WITH TWO REAL IMPLEMENTATIONS.
//
// This is deliberately NOT in win32_api.h, by that header's own rule: it is the
// home for calls with no non-Windows meaning, where "off Windows these are no-ops
// or honest failures". GetOpenFileNameW and GetSaveFileNameW have a genuine
// counterpart -- QFileDialog -- so this follows platform_paths.h and keyboard.h
// instead: a declaration here, one .cpp per platform, and CMake picking the file.
// That is how the tree already distinguishes "both answers are real" from "one
// answer is a stand-in", and the distinction is what makes win32_api.h's rule
// enforceable rather than decorative.
//
// WHAT THE CENSUS FOUND, AND WHY IT SHAPED THE SIGNATURE
//
// Five sites, measured 2026-09-15 by SHAPE rather than by name, because five
// occurrences of OPENFILENAMEW are not five copies of the same call.
//
// THE ADDRESSES BELOW ARE HISTORY AS OF 2026-09-20, not live pointers, for the same
// reason clipboard.h's census table says so at more length: this is the PRE-ROUTING
// population and those lines now hold the seam call rather than the raw one. A dated
// census may age; a live citation may not.
//
//   site                     kind  filters  initial name  defExt   reads index
//   editor.cpp:1048          Save  3 static  --           L"md"    no
//   editor.cpp:1079          Open  3 static  --           --       no
//   export_html.cpp:1468     Save  3 static  doc stem     --       YES
//   input.cpp SET_LOCATE_PANDOC  Open  2 static  --       --       no
//   pandoc.cpp:205           Save  1 DYNAMIC doc stem+ext  dynamic  no
//
// The two columns on the right are the whole reason this header is not a
// one-argument function.
//
// THE FILTER INDEX IS RETURNED BECAUSE ONE SITE IN FIVE READS IT.
// export_html.cpp:1478 branches on ofn.nFilterIndex to choose between HTML, DOCX
// and PDF. A seam that handed back only a path would compile at all five sites,
// pass every test in the tree, and silently make every export an HTML file
// regardless of what the user picked -- the failure would be invisible until
// somebody exported a Word document and got HTML with a .docx name. A census by
// name would have missed it; that is what "enumerate call SHAPES, not names" buys.
//
// It is 1-BASED, matching OPENFILENAMEW::nFilterIndex exactly, so that site's
// `== 2` and `== 3` comparisons are untouched. Renumbering from zero would be a
// tidier API and an off-by-one waiting at the one site that cares.
//
// THE FLAG DIVERGENCE IS CARRIED, NOT UNIFIED. A seam preserves behaviour; it does
// not improve it. Two flags differ across the five sites and each is a real
// difference in what the dialog does:
//
//   OFN_NOCHANGEDIR   3 of 5 (editor x2, export_html). Absent at SET_LOCATE_PANDOC
//                     and
//                     pandoc.cpp:205, which therefore DO let the process's current
//                     directory follow the user's browsing. Unifying that would
//                     change where a later relative path resolves.
//   OFN_HIDEREADONLY  1 of 5 (editor.cpp:1079 only). Hides the read-only checkbox.
//
// Those two are fields below. The rest do NOT vary and live in the Windows
// backend, which is a measurement and not a convenience: all three Save sites pass
// exactly OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, and both Open sites pass exactly
// OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST. Putting an invariant in a field would
// invite a caller to vary it, which is the opposite of what this seam is for.
//
// WHAT THE QT HALF CANNOT DO, SAID HERE RATHER THAN DISCOVERED AT THE CUTOVER
//
//   * There is no owner, so the dialog is application-modal, not window-modal. The
//     request carried an HWND `owner` until 2026-10-14, and every site filled it with
//     App::hwnd, which nothing assigns, so it never named a window. It went with the
//     HWND type. A window-modal dialog would take its parent from the window sink,
//     where the QWidget is, rather than from each caller.
//   * `restoreDirectory` and `hideReadOnly` are both no-ops in Qt: QFileDialog does
//     not change the process directory at all, and it has no read-only checkbox.
//     Ignoring a flag whose effect does not exist is honest; the fields stay because
//     the WINDOWS behaviour they name is real and must not drift.
//
// PATHS ARE STILL CAPPED AT MAX_PATH by the Windows backend, exactly as all five
// sites were. Raising it is a behaviour change (long paths that truncate today
// would start working), and a portability seam is the wrong commit for one.
namespace platform {

// One row of a file-type filter: what the user reads, and what it matches.
//
// std::wstring rather than const wchar_t*, and pandoc.cpp:205 is the reason: it
// builds both halves at runtime from a PandocFormat, so a non-owning row would
// point at a temporary. Four of the five sites could use pointers and the fifth
// could not, and a seam that is safe at four sites out of five is not safe.
struct FileFilter {
    std::wstring description;  // e.g. L"Markdown (*.md)"
    std::wstring patterns;     // e.g. L"*.md;*.markdown" -- semicolon separated
};

struct FileDialogRequest {
    // At least one row. An empty list would ask the platform for a dialog that
    // matches nothing, which no site does and neither backend defines.
    std::vector<FileFilter> filters;

    // Pre-filled file name, WITHOUT a directory. Empty at three of five sites.
    std::wstring initialName;

    // Appended when the user types a name with no extension. Empty means "do not
    // append", and the Windows backend passes nullptr for it rather than L"":
    // lpstrDefExt is documented as nullptr-for-none, and an empty string is a
    // different input.
    std::wstring defaultExtension;

    // OFN_NOCHANGEDIR: put the process's current directory back afterwards.
    bool restoreDirectory = false;

    // OFN_HIDEREADONLY: hide the read-only checkbox. Open dialogs only.
    bool hideReadOnly = false;
};

struct FileDialogResult {
    std::wstring path;

    // 1-BASED, as OPENFILENAMEW::nFilterIndex is. See the header note.
    int filterIndex = 1;
};

// Show the picker. std::nullopt means the user cancelled.
//
// nullopt-for-cancel is not a new state invented for this seam: it is what all
// five sites already handle, because GetOpenFileNameW/GetSaveFileNameW returning
// FALSE is the same condition, and each site returns without touching anything.
// This is win32_api.h's rule about postWindowMessage applied one layer up -- when a
// call already has a documented failure mode its callers handle, an off-Windows
// path should USE that mode rather than invent a third one.
std::optional<FileDialogResult> requestOpenPath(const FileDialogRequest& request);
std::optional<FileDialogResult> requestSavePath(const FileDialogRequest& request);

// --- The two filter encodings, split out so they can be asserted ---------------
//
// SPLIT OUT FOR THE REASON keyboard.h SPLITS OUT modifierHeld. The two functions
// above cannot be asserted to say anything: a test cannot click a dialog, so the
// only reachable claim is "it returns nullopt when nobody picks a file", which a
// stub that ignores the filesystem satisfies just as well.
//
// The part with actual decisions in it is the ENCODING, and it is pure. Windows
// wants one buffer of NUL-separated strings terminated by a second NUL -- a shape
// that is easy to get subtly wrong and impossible to see wrong in a debugger, since
// every printer stops at the first NUL. Qt wants a single string of `;;`-separated
// rows with space-separated patterns in parentheses. Both are total functions of the
// same input, so both can be asserted exhaustively on either platform, and a stub
// cannot pass them.
//
// Having both here, rather than each inside its own backend, is deliberate: it lets
// one test assert that the two encodings describe the SAME filter set, which is the
// property that will actually matter when Windows switches from the first to the
// second.
//
// They are `inline` in the header rather than in a third source file, again as
// modifierHeld is, and the consequence is the point: a test can assert both
// encodings while linking NEITHER backend, so the assertions cannot be satisfied by
// whichever platform half happens to be selected.

// The Windows lpstrFilter buffer: description NUL patterns NUL ... NUL NUL.
// Returns an empty string for an empty list -- see the note on FileDialogRequest
// about that being a shape no site has.
//
// The double NUL at the end is what the four literal filters in the tree got from a
// string literal's own implicit terminator, which is why none of them wrote it out.
// input.cpp's SET_LOCATE_PANDOC DID write it, so its buffer ends in three NULs; Windows stops at the
// second, so producing two here is the same input.
inline std::wstring buildWin32FilterBuffer(const std::vector<FileFilter>& filters) {
    if (filters.empty()) return std::wstring();
    std::wstring out;
    for (const FileFilter& filter : filters) {
        out.append(filter.description);
        out.push_back(L'\0');
        out.append(filter.patterns);
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

// The Qt nameFilter string: `Description (*.a *.b);;Other (*.c)`.
//
// The description is emitted as-is and the patterns are appended in parentheses,
// which DOUBLES the parenthetical for the four sites whose description already ends
// in one -- L"Markdown (*.md)" becomes L"Markdown (*.md) (*.md *.markdown)". That is
// ugly and it is the honest reading: Qt derives its display text from the whole
// filter string, so the alternatives are to strip a trailing parenthetical by
// heuristic, or to change the description text the Windows dialog shows today. The
// first guesses, the second is a behaviour change inside a portability seam. Left
// visible in a test instead, to be settled when the Qt dialog becomes the one users
// see.
inline std::wstring buildQtNameFilter(const std::vector<FileFilter>& filters) {
    std::wstring out;
    for (std::size_t i = 0; i < filters.size(); i++) {
        if (i > 0) out.append(L";;");
        out.append(filters[i].description);
        out.append(L" (");
        // Qt separates patterns with spaces where Windows uses semicolons. Done as a
        // copy-and-substitute rather than a split-and-join because a semicolon is the
        // only separator Windows accepts, so there is nothing else to recognise.
        std::wstring patterns = filters[i].patterns;
        for (wchar_t& c : patterns) {
            if (c == L';') c = L' ';
        }
        out.append(patterns);
        out.push_back(L')');
    }
    return out;
}

}  // namespace platform

#endif  // TINTA_FILE_DIALOG_H

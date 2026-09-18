// The Windows half of the file-picker seam (include/file_dialog.h):
// GetOpenFileNameW and GetSaveFileNameW, with the five call sites' behaviour
// preserved exactly rather than tidied.
//
// This file is the ONLY place under src/ that includes <commdlg.h> for a file
// dialog. src/print.cpp still includes it and must keep it -- its PRINTDLGW and
// PrintDlgW are a different dialog with a different struct, and folding them in here
// would be a second seam wearing the first one's name.
//
// THE LINE NUMBERS THAT USED TO BE HERE ARE DELETED RATHER THAN UPDATED, and that is the
// point of this paragraph. It said "at print.cpp:580 and :603"; they are at 649 and 672
// today, moved by an unrelated change to a function four hundred lines earlier
// (drawDocumentRange taking a tinta::RenderTarget&). A line number in a comment is a
// claim about a file this comment cannot see, it goes stale on every edit ABOVE the line
// rather than at the line, and nothing fails when it does. The two symbol names are
// greppable and cannot drift; that is what a cross-file reference should be.

#include "file_dialog.h"

#include <commdlg.h>
#include <wchar.h>

namespace platform {
namespace {

// One implementation for both directions, because the census says the two differ in
// exactly one expression: the invariant flag set, and which of the two API functions
// is called. Everything else -- the filter buffer, the MAX_PATH buffer, the
// pre-filled name, the default extension, the two divergent flags, the result -- was
// character-for-character identical across all five sites.
std::optional<FileDialogResult> show(const FileDialogRequest& request, bool save) {
    // Held in a named local, not a temporary: lpstrFilter is a borrowed pointer and
    // the dialog reads it after this statement.
    std::wstring filter = buildWin32FilterBuffer(request.filters);

    // MAX_PATH, as all five sites had it. The truncation this can cause is
    // pre-existing behaviour and raising the cap is deliberately not done here --
    // see the note in file_dialog.h.
    wchar_t path[MAX_PATH] = L"";
    if (!request.initialName.empty()) {
        wcsncpy_s(path, request.initialName.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = request.owner;
    ofn.lpstrFilter = filter.empty() ? nullptr : filter.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    // nullptr for none, NOT L"": lpstrDefExt is documented as a pointer that may be
    // null, and an empty string is a different input to the dialog.
    ofn.lpstrDefExt =
        request.defaultExtension.empty() ? nullptr : request.defaultExtension.c_str();

    // The invariant flags, measured across the five sites rather than chosen: every
    // Save site passed exactly these two, and so did every Open site. They are here
    // and not in the request struct because a field invites a caller to vary it.
    DWORD flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
                       : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (request.restoreDirectory) flags |= OFN_NOCHANGEDIR;
    if (request.hideReadOnly) flags |= OFN_HIDEREADONLY;
    ofn.Flags = flags;

    // nFilterIndex is left at 0 going in, which is what all five sites did: zero
    // means "no custom filter, so use the first one". Coming out it is the 1-based
    // index of what the user chose, which is the value export_html.cpp branches on.
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return std::nullopt;

    FileDialogResult result;
    result.path = path;
    result.filterIndex = (int)ofn.nFilterIndex;
    return result;
}

}  // namespace

std::optional<FileDialogResult> requestOpenPath(const FileDialogRequest& request) {
    return show(request, false);
}

std::optional<FileDialogResult> requestSavePath(const FileDialogRequest& request) {
    return show(request, true);
}

}  // namespace platform

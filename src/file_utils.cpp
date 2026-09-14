#include "file_utils.h"
#include "document.h"
#include "platform_paths.h"
#include "utils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

// STILL WINDOWS-ONLY, DELIBERATELY, AND THE ONLY THING IN THIS FILE THAT IS.
//
// Not left behind for lack of a portable spelling -- std::filesystem::last_write_time
// is right there. Left behind because the thing it writes into, App::lastFileWriteTime,
// is a FILETIME, and FILETIME is a tree-wide member type: app.h declares two (1186
// and LayoutTab::lastWrite at 539), input.cpp reads and compares five, tabs.cpp
// copies one, and startpage.cpp converts three to local time for DISPLAY. That last
// one is what makes this a decision rather than an edit. Changing the representation
// to something portable means changing all eleven sites together, and the obvious
// cheap move -- keeping the FILETIME shape off Windows and packing a POSIX timestamp
// into its two halves -- would satisfy the equality comparisons in input.cpp and
// silently produce garbage dates on the start page.
//
// So it is not ported and it is not stubbed. A no-op #else here would compile and
// then never detect an external file change off Windows, which is the failure mode
// this tree keeps writing down. Failing to compile is the accurate report: this
// function has no portable implementation until FILETIME does.
void updateFileWriteTime(App& app) {
    if (app.currentFile.empty()) return;
    std::wstring widePath = toWide(app.currentFile);
    HANDLE h = CreateFileW(widePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        GetFileTime(h, nullptr, nullptr, &app.lastFileWriteTime);
        CloseHandle(h);
    }
}

bool isRootPath(const std::wstring& path) {
    if (path.length() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return true;
    }
    return false;
}

std::wstring getParentPath(const std::wstring& path) {
    if (path.empty()) return path;
    // Remove trailing slash if present
    std::wstring p = path;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
        p.pop_back();
    }
    // Find last separator
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return p;
    // Handle root case (C:\ -> C:\)
    if (pos == 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring getDirectoryFromFile(const std::string& filePath) {
    std::wstring wide = toWide(filePath);
    size_t pos = wide.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return wide.substr(0, pos);
}

void populateFolderItems(App& app) {
    app.folderItems.clear();
    app.hoveredFolderIndex = -1;
    app.folderBrowserScroll = 0.0f;

    if (app.folderBrowserPath.empty()) return;

    // Add ".." entry if not at root
    if (!isRootPath(app.folderBrowserPath)) {
        app.folderItems.push_back({L"..", true});
    }

    std::vector<App::FolderItem> folders;
    std::vector<App::FolderItem> files;

    // std::filesystem rather than FindFirstFileW/FindNextFileW/FindClose. The glob
    // this used to build ("dir\\*") went with them: directory_iterator enumerates a
    // directory directly, so there was never a pattern to match, only an API that
    // demanded one.
    //
    // The error_code overload matters. Without it a directory the user cannot read
    // THROWS, and populateFolderItems has no handler -- the old code returned
    // quietly on INVALID_HANDLE_VALUE, so throwing would be a new crash on an
    // unreadable folder. With it, construction fails into an end iterator, the loop
    // body never runs, and the listing comes back empty exactly as before.
    std::error_code ec;
    const std::filesystem::path dir(app.folderBrowserPath);
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::wstring name = entry.path().filename().wstring();

        // Navigation entries, not content. FindFirstFileW yielded these and
        // directory_iterator does not, but the check stays: it is what tells
        // platform::isHiddenEntry it will never be asked about them, which is a
        // promise that file's POSIX implementation relies on to treat a leading dot
        // as meaning hidden.
        if (name.empty() || name == L"." || name == L"..") continue;

        if (platform::isHiddenEntry(entry.path().wstring(), name)) continue;

        // Same error_code discipline: is_directory() on a dangling symlink or a
        // vanished entry throws otherwise, and mid-enumeration is precisely when
        // that happens. A failed query leaves `isDir` false, so the entry falls
        // through to the document-extension test and is dropped unless it looks
        // like something we can open -- which is the safe direction.
        std::error_code entryEc;
        if (entry.is_directory(entryEc)) {
            folders.push_back({name, true});
        } else if (isSupportedDocumentPath(name)) {
            files.push_back({name, false});
        }
    }

    // Sort alphabetically, case-insensitively, replacing _wcsicmp -- an MSVC
    // extension with no POSIX spelling.
    //
    // Folded locally rather than through platform::documentPathsEqual, and the
    // distinction is the same one themes.cpp draws at length. That seam answers "are
    // these two names one file", where Windows and Linux genuinely disagree and both
    // are right. This asks "which name comes first in a list a human is reading",
    // where a platform-dependent answer would just be an inconsistency -- the same
    // folder should not reorder itself because of the OS.
    //
    // Windows ordering is unchanged: _wcsicmp folds per code unit under LC_CTYPE,
    // tinta never calls setlocale, so it ran in the "C" locale folding ASCII and
    // nothing else, which is what towlower does here on the same input.
    auto cmpFunc = [](const App::FolderItem& a, const App::FolderItem& b) {
        const std::wstring& x = a.name;
        const std::wstring& y = b.name;
        const size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; i++) {
            const std::wint_t cx = std::towlower(static_cast<std::wint_t>(x[i]));
            const std::wint_t cy = std::towlower(static_cast<std::wint_t>(y[i]));
            if (cx != cy) return cx < cy;
        }
        // Equal on the shared prefix, so the shorter name sorts first. This keeps
        // the comparator a strict weak ordering, which std::sort requires and which
        // `_wcsicmp(...) < 0` also gave.
        return x.size() < y.size();
    };
    std::sort(folders.begin(), folders.end(), cmpFunc);
    std::sort(files.begin(), files.end(), cmpFunc);

    // Add folders first, then files
    for (auto& f : folders) app.folderItems.push_back(std::move(f));
    for (auto& f : files) app.folderItems.push_back(std::move(f));
}

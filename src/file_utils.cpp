#include "file_utils.h"
#include "document.h"
#include "platform_paths.h"
#include "utils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

// PORTABLE SINCE THE FILETIME SEAM LANDED, AND THE COMMENT THAT USED TO SIT HERE
// IS CORRECTED IN PLACE RATHER THAN DELETED, BECAUSE ITS PREMISE WAS RIGHT AND ONLY
// ITS CONCLUSION WAS WRONG.
//
// What it said: App::lastFileWriteTime is a FILETIME, FILETIME is a tree-wide member
// type (app.h declares two, input.cpp touches five values, tabs.cpp copies one,
// startpage.cpp converts three for DISPLAY), so porting this function means changing
// the representation across all eleven sites -- and the cheap move, keeping the
// FILETIME shape off Windows and packing a POSIX timestamp into its two halves, would
// satisfy every equality comparison in input.cpp and silently redate the start page
// by 369 years.
//
// Every word of that is still true, INCLUDING the counts: a census by type NAME finds
// three in input.cpp and none in tabs.cpp, because `tab.lastWrite = app.lastFileWriteTime`
// names no type. The value census is the correct one and it is the one above.
//
// What it got wrong is that packing was the only portable move. It was not, and the
// counter-example was already in the tree: settings.cpp's fileTimeTicksNow() computes
// GENUINE FILETIME ticks -- 100ns intervals since 1601 -- out of std::chrono, on any
// platform, and settings.h says in as many words why it must (settings.ini's [Recent]
// timestamps already hold that epoch and the start page decodes them with
// FileTimeToLocalFileTime). So the answer is not to change what a file time MEANS off
// Windows; it is to produce the meaning the tree already has. platform::fileWriteTicks
// does that, all eleven sites are untouched, and nothing here is stubbed or no-oped.
void updateFileWriteTime(App& app) {
    if (app.currentFile.empty()) return;
    const unsigned long long ticks = platform::fileWriteTicks(toWide(app.currentFile));

    // 0 is "could not be read", and the member is LEFT ALONE rather than zeroed.
    // That is the original's behaviour exactly -- it skipped the assignment when
    // CreateFileW failed -- and it is load-bearing: input.cpp compares this value
    // for inequality to decide the file changed underneath us, so zeroing it on a
    // transient open failure would report an external edit that never happened.
    if (ticks == 0) return;

    // Split by shifting rather than through a ULARGE_INTEGER, which win32_types.h
    // does not stand in off Windows. dwLowDateTime/dwHighDateTime are unsigned long
    // in both the real header and the stand-in, so the casts are exact.
    app.lastFileWriteTime.dwLowDateTime =
        static_cast<unsigned long>(ticks & 0xFFFFFFFFULL);
    app.lastFileWriteTime.dwHighDateTime =
        static_cast<unsigned long>(ticks >> 32);
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

// Read a document off disk and put it in App. The Qt entry point's whole command line.
//
// WHY IT IS A NAMED FUNCTION IN A PORTABLE FILE rather than the body of an `if` in
// main(). src/main_d2d.cpp has the other shape and it is the reason this function had
// to be written at all: the equivalent logic there is two lambdas INSIDE WinMain --
// `loadDocumentContent` at :1765 and `loadFile` at :1777 -- so nothing outside that
// function can call it, no test can reach it, and src/main_qt.cpp could not reuse a
// line of it. Eleven lines of duplication was the cheap option; this is the one that
// leaves a caller for the second frontend and a target for a test.
//
// THE COMMENT IN src/main_qt.cpp SAID THIS WAS "four lines here that read argv[1]",
// and that estimate was wrong -- corrected in place there rather than deleted, since
// it was an honest reading of a call it turned out nobody could make.
//
// THE ifstream IS THE INTERESTING PART, and it is C58's fix pattern rather than
// another instance of C58's defect. main_d2d.cpp's loadFile does this:
//
//     std::wstring widePath = toWide(path);
//     std::ifstream file(widePath);          // <-- MSVC extension, no POSIX spelling
//
// Constructing a stream directly from a std::wstring is a Microsoft extension; it is
// one of the twenty sites C58 still owns and it does not compile off Windows. Going
// through std::filesystem::path first is STANDARD C++17 and portable, and it keeps the
// wide path all the way to the OS on Windows, so non-ASCII filenames still open --
// which is the property the extension was there for and the reason the sites cannot
// simply be narrowed to std::string. Every remaining C58 site can take this shape.
//
// ANNOTATIONS ARE DELIBERATELY NOT PARSED HERE, and that is a divergence from
// main_d2d.cpp rather than an oversight: loadDocumentContent calls
// annotationsParseSource(app) (issue #126, review annotations derive from the raw
// source), src/annotations.cpp is not in tests/portable-sources.txt -- 33 native
// mentions at the last census -- and calling it would make THIS file unportable and
// take file_utils.cpp out of the manifest with it. app.sourceText is still assigned,
// so the input that pass needs is present and the call is the only thing missing. When
// annotations.cpp ports, one line lands here and this paragraph goes with it.
bool loadDocumentFromPath(App& app, const std::string& path) {
    std::ifstream file(std::filesystem::path(toWide(path)), std::ios::binary);
    if (!file) return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    // std::string_view overload, so the path reaches isMermaidDocumentPath and
    // isPlainTextDocumentPath as written -- a .mmd or a .txt argument gets the same
    // treatment it gets on the Windows path, which is a thing worth NOT reimplementing.
    const qmd::ParseResult result = parseDocument(app.parser, content, std::string_view(path));
    if (!result.success) return false;

    app.root = result.root;
    app.parseTimeUs = result.parseTimeUs;
    app.sourceText = content;
    return true;
}

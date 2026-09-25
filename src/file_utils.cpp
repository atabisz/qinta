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
// Constructing a stream directly from a std::wstring is a Microsoft extension; it does
// not compile off Windows. Going through std::filesystem::path first is STANDARD C++17
// and portable, and it keeps the wide path all the way to the OS on Windows, so
// non-ASCII filenames still open -- which is the property the extension was there for
// and the reason the sites cannot simply be narrowed to std::string. Every remaining C58
// site can take this shape.
//
// THE FIGURE THAT STOOD HERE -- "one of the twenty sites C58 still owns" -- WAS NEVER A
// COUNT OF SITES, and this is C58's derived population, taken 2026-09-20. The tree
// carried "twenty" in three places and "22" in two, and BOTH UNDERSTATED, which is the
// dangerous direction: a low number reads as nearly-finished work. Classifying every
// stream construction under src/ and include/ outside the Qt backends by its argument
// type gives 33 constructions = 19 already portable (16 through fs::path, 3 taking a
// narrow string or a path already) + 14 live C58.
//
// WHY THE TRACKED FIGURES COULD NOT HAVE BEEN RIGHT: 9 of those 14 sat behind a FATAL
// include, so a figure read off compiler output was structurally unable to see them.
// input.cpp was itself a fatal census row until 2026-09-20 (5 sites), export_docx.cpp was
// fatal on objbase.h (3) until later the same day, and print.cpp (1) still is -- on
// d2d1.h, where this sentence said commdlg.h until the PDF seam put <d2d1.h> above
// print.cpp's own <commdlg.h>. The census names only the first header it cannot resolve,
// so the name in a sentence like this one is the perishable part and the fatality is not:
// print.cpp's row still reads exit 1, fatal 1, errors 0, and the 1 site is still invisible
// to g++. The export_docx.cpp clause is worth reading twice: that file was NEVER genuinely
// behind objbase.h -- the header was dead, three lines of text with no caller, and
// deleting it made the file compile off Windows unchanged. So this paragraph's point
// stands and gets sharper. A figure read off compiler output could not see those 3 sites,
// and the reason it could not see them was not even a real dependency. Hence the rule this
// derivation
// follows: THE SOURCE IS THE DENOMINATOR AND THE COMPILER IS THE CROSS-CHECK, never the
// reverse. Measured, not feared -- one site in pandoc.cpp was masked even in a file g++
// could read, because an unrelated error on its path variable's own declaration
// suppressed the diagnostic on its use.
//
// C58 IS NOW CLOSED IN EVERY FILE DESTINED TO BE PORTABLE. The 11 live sites in
// input.cpp, pandoc.cpp, export_docx.cpp and print.cpp were fixed on 2026-09-20. THREE
// REMAIN, each for a stated reason rather than because nobody counted it: main_d2d.cpp's
// loadFile above, which the Qt cutover deletes; image_win32.cpp's, paired with
// image_qt.cpp through CMake's PLATFORM_IMAGE_SOURCE so it is never compiled off
// Windows, and documented as per-backend at the site; and chrome_dump_d2d.cpp's, which
// goes with the D2D backend. Re-derive rather than quote: the classification is a grep
// for stream constructions plus a read of each argument's declared type.
//
// ANNOTATIONS ARE STILL DELIBERATELY NOT PARSED HERE, and the paragraph that used to
// sit here PREDICTED THE WRONG TRIGGER, so it is corrected in place rather than
// deleted. It said:
//
//     "src/annotations.cpp is not in tests/portable-sources.txt -- 33 native mentions
//      at the last census -- and calling it would make THIS file unportable and take
//      file_utils.cpp out of the manifest with it. [...] When annotations.cpp ports,
//      one line lands here and this paragraph goes with it."
//
// annotations.cpp DID port, 2026-09-17, and it IS in tests/portable-sources.txt. The
// line did not land, because compiling and linking are different claims and the old
// wording conflated them. annotations.cpp sits in the manifest's COMPILE-ONLY section:
// it compiles clean under g++, and it cannot link into a portable target because it
// calls handleFileWatchTimer, which lives in input.cpp, which windows.h makes fatal.
// So it is in no target's link closure -- portable_compile_only is an OBJECT library
// that nothing links -- and the symbol this call needs does not exist off Windows.
//
// MEASURED, NOT REASONED, because this tree has already been burned by an asserted
// link failure (see the search.cpp note in CMakeLists.txt: "arrived at by attempting
// the link and reading the errors"). The call and its include were added here, the
// Linux build was run, and the reading was THREE broken targets rather than the two
// the old paragraph implied:
//
//     undefined reference to `annotationsParseSource(App&)'
//       CMakeFiles/qinta.dir/src/file_utils.cpp.o
//       CMakeFiles/qt_entry_tests.dir/src/file_utils.cpp.o
//       CMakeFiles/file_utils_tests.dir/src/file_utils.cpp.o
//
// The third is the one worth naming: file_utils_tests is this file's OWN test, so the
// cost is not only that qinta stops linking, it is that the file loses the target that
// asserts it. The object file itself compiled in all three -- the block below is
// reachable, the input it needs is present, and only the definition is missing.
//
// SO THE TRIGGER IS NOT "annotations.cpp compiles", IT IS "annotationsParseSource is
// in a portable link closure", which needs annotations.cpp's own dependency on
// input.cpp's file-watch entry point broken first. That is a separate slice and a
// bigger one than this line. app.sourceText is still assigned above, so when the
// symbol arrives this stays a one-line change.
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

    // WHICH FILE IS OPEN, and src/input.cpp asks constantly. Missing until slice 3 (the
    // mouse router), and nothing noticed while the keyboard was the only input: every left
    // press on an open document went to the START PAGE, because startPageActive() is
    // `currentFile.empty() && ...`, and the start page consumes the click. The Win32 loader,
    // openDocumentInViewer, assigns it from the same UTF-8 spelling. What that loader also
    // does -- recents, reading-position memory -- stays out: both write settings.ini, and
    // this function has been side-effect-free on disk since it was written.
    app.currentFile = path;

    // THE CACHED LAYOUT BELONGS TO THE DOCUMENT THAT JUST GOT REPLACED, AND NOTHING
    // DOWNSTREAM NOTICES ON ITS OWN. QtAppWindow::paintDocument relayouts only when the
    // widget resized or the run list is empty, so opening a SECOND file into a window
    // that has already painted would draw the first file's runs against the second
    // file's root -- a window that ignores Ctrl+O, which reads as a broken dialog rather
    // than a stale cache. The Windows path gets this for free from app.layoutDirty,
    // which its message loop checks every frame and this stack has no equivalent of yet.
    //
    // Invalidating HERE rather than in the caller, because the rule belongs to the
    // assignment above: anything that replaces app.root has invalidated the layout, and
    // a caller that has to remember to invalidate is a caller that will forget. Asserted
    // in tests/qt_entry_tests.cpp by loading two different documents in a row and
    // reading the pixels, which is the only shape of test that fails if this is missed.
    //
    // THE FLAG IS NOT OPTIONAL AND THE PARAGRAPH ABOVE IS WHERE I LEARNED IT. This code
    // called clearLayoutCache() alone first, and the sentence above already named
    // app.layoutDirty as the mechanism that does this job properly -- while the code did
    // not set it. clearLayoutCache() empties the run vectors and touches neither
    // layoutDirty nor layoutComplete nor contentHeight (include/app.h:1395), so
    // render.cpp:3275's ensureLayoutComplete saw a layout that was neither dirty nor
    // incomplete and RETURNED WITHOUT LAYING ANYTHING OUT. The End key then clamped
    // against a contentHeight belonging to the document before this one -- 316.8 against
    // a 6164-pixel document -- and landed at the top instead of the bottom, silently.
    // qt_entry_tests run 9 caught it, and only because it presses End after a load with
    // no paint in between; every earlier End in that file ran after a paint had already
    // rebuilt the layout, so they all passed on a stale-but-correct value.
    app.clearLayoutCache();
    app.layoutDirty = true;

    // And the geometry those two flags describe, zeroed rather than left at the previous
    // document's figures. Nothing downstream requires this -- layoutDocument overwrites
    // both -- but a scroll arriving between this load and the next layout clamps against
    // whatever is here, and clamping a new document against the LAST one's height puts
    // the view somewhere unrelated to either. Zero clamps it to the top, which is where
    // a freshly opened document belongs and what include/scroll.h already documents as
    // the pre-layout behaviour.
    app.contentHeight = 0.0f;
    app.contentWidth = 0.0f;

    // A new document starts at the top. Both members, because src/input.cpp's scroll
    // sites and the clamp in include/scroll.h read and write the pair.
    app.scrollY = 0.0f;
    app.scrollX = 0.0f;
    app.targetScrollY = 0.0f;
    app.targetScrollX = 0.0f;

    return true;
}

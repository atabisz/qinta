// Tests src/file_utils.cpp off Windows, and that is new in two ways worth stating
// separately, because they are different claims.
//
// The file COMPILES off Windows only as of the FILETIME seam: updateFileWriteTime
// was its last Windows-bound function, and platform::fileWriteTicks replaced the
// CreateFileW/GetFileTime/CloseHandle block. That is a portability claim, and the
// manifest in tests/portable-sources.txt is what asserts it.
//
// The file is EXECUTED by a test for the first time here, on either platform, which
// is the larger of the two. All five externally reachable functions had zero
// coverage: three are pure string arithmetic that has been quietly carrying Windows
// drive-letter assumptions, one enumerates a directory, and updateFileWriteTime is
// the one the whole seam was built for. A function nothing runs is not a function
// anyone knows the behaviour of, which is why several assertions below pin behaviour
// that is arguably WRONG on Linux rather than quietly correcting it -- each one says
// so, and changing any of them changes what the application does.

#include "app.h"
#include "file_utils.h"
#include "platform_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

// FILETIME halves rejoined, the way input.cpp's CompareFileTime effectively reads
// them. Local to the test on purpose: if file_utils.cpp's split were ever changed to
// share a helper with this, the two would agree by construction and prove nothing.
unsigned long long joined(const FILETIME& ft) {
    return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
           static_cast<unsigned long long>(ft.dwLowDateTime);
}

void writeFile(const std::filesystem::path& p, const char* body) {
    std::ofstream out(p, std::ios::binary);
    out << body;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const std::wstring stamp = std::to_wstring(
        std::chrono::system_clock::now().time_since_epoch().count());
    std::error_code error;
    const fs::path probeDir = fs::temp_directory_path() / (L"tinta-fileutils-" + stamp);
    check(fs::create_directories(probeDir, error) && !error,
          "temporary probe directory is created");

    // --- isRootPath ---------------------------------------------------------
    // ALL FOUR OF THESE PIN A DRIVE-LETTER RULE, and on Linux and macOS that rule
    // can never be satisfied by a real path. The consequence is not cosmetic:
    // populateFolderItems adds a ".." entry whenever isRootPath is false, so at "/"
    // on Linux the folder browser will offer a parent that does not exist. That is
    // asserted rather than fixed because fixing it changes what the application
    // shows, which is not this slice's call to make.
    check(isRootPath(L"C:\\"), "a backslashed drive root is a root");
    check(isRootPath(L"C:/"), "a forward-slashed drive root is a root too");
    check(!isRootPath(L"C:\\Users"), "a path below the drive root is not a root");
    check(!isRootPath(L"C:"), "a bare drive letter with no separator is not a root");
    check(!isRootPath(L"/"),
          "POSIX \"/\" is NOT recognised as a root -- pinned, not endorsed: the "
          "folder browser therefore offers \"..\" at the filesystem root on Linux");
    check(!isRootPath(L""), "an empty path is not a root");

    // --- getParentPath ------------------------------------------------------
    check(getParentPath(L"") == L"", "empty in, empty out");
    check(getParentPath(L"C:\\Users\\alex") == L"C:\\Users",
          "the last component is removed");
    check(getParentPath(L"C:\\Users\\alex\\") == L"C:\\Users",
          "a trailing separator is stripped before the parent is taken, so a "
          "directory path does not return itself");
    check(getParentPath(L"C:\\Users\\alex\\\\\\") == L"C:\\Users",
          "SEVERAL trailing separators are stripped, not one -- the loop, not an if");
    check(getParentPath(L"C:\\Users") == L"C:\\",
          "the parent of a first-level directory keeps the drive's separator, so it "
          "is a usable path and not the bare \"C:\"");
    check(getParentPath(L"/home/alex") == L"/home",
          "POSIX separators work: find_last_of takes both");
    check(getParentPath(L"alex") == L"alex",
          "a name with no separator at all is returned unchanged -- NOT empty, which "
          "is what a caller looping on getParentPath depends on to terminate");
    check(getParentPath(L"/home") == L"",
          "and the POSIX root case is EMPTY, not \"/\" -- the drive-letter branch "
          "above has no POSIX counterpart. Pinned, not endorsed, same as isRootPath");

    // --- getDirectoryFromFile -----------------------------------------------
    check(getDirectoryFromFile("/home/alex/notes.md") == L"/home/alex",
          "the directory of a POSIX file path");
    check(getDirectoryFromFile("C:\\docs\\notes.md") == L"C:\\docs",
          "the directory of a Windows file path");
    check(getDirectoryFromFile("notes.md") == L".",
          "a bare filename yields \".\" rather than an empty string");
    check(getDirectoryFromFile("") == L".",
          "so does an empty path -- there is no separator to find");

    // --- updateFileWriteTime ------------------------------------------------
    // The function the FILETIME seam was built for, and the first execution of it in
    // a test on any platform.
    {
        App app;
        const fs::path doc = probeDir / L"document.md";
        writeFile(doc, "# heading\n");

        // POISONED, not zeroed. Zero is this member's initial value AND the seam's
        // "cannot be read" answer, so a poisoned pattern is the only way to tell
        // "was written" apart from "was left alone" in the two assertions below.
        app.lastFileWriteTime.dwLowDateTime = 0xDEADBEEFul;
        app.lastFileWriteTime.dwHighDateTime = 0x0BADF00Dul;
        const unsigned long long poison = joined(app.lastFileWriteTime);

        app.currentFile.clear();
        updateFileWriteTime(app);
        check(joined(app.lastFileWriteTime) == poison,
              "an empty currentFile leaves the member untouched");

        app.currentFile = (probeDir / L"no-such-document.md").string();
        updateFileWriteTime(app);
        check(joined(app.lastFileWriteTime) == poison,
              "A MISSING FILE LEAVES THE MEMBER UNTOUCHED and does not zero it -- "
              "input.cpp compares this value for inequality, so zeroing it on a "
              "failed open would report an external edit that never happened");

        app.currentFile = doc.string();
        updateFileWriteTime(app);
        const unsigned long long written = joined(app.lastFileWriteTime);
        check(written != poison, "a readable file overwrites the poison");
        check(written == platform::fileWriteTicks(doc.wstring()),
              "and what it writes is exactly the seam's value, reassembled from the "
              "two halves -- the shift and mask in file_utils.cpp are correct");
        check(app.lastFileWriteTime.dwHighDateTime != 0,
              "CONTROL: the high half is populated, so the equality above is not "
              "passing on a value that fits in 32 bits either way");
    }

    // --- populateFolderItems ------------------------------------------------
    {
        App app;
        const fs::path listing = probeDir / L"listing";
        check(fs::create_directories(listing, error) && !error,
              "listing directory is created");
        check(fs::create_directories(listing / L"Beta", error) && !error,
              "a subdirectory is created");
        check(fs::create_directories(listing / L"alpha", error) && !error,
              "a second subdirectory, lowercase, is created");
        writeFile(listing / L"Zebra.md", "z\n");
        writeFile(listing / L"apple.md", "a\n");
        writeFile(listing / L"notes.txt", "n\n");
        writeFile(listing / L"binary.bin", "\x01\x02");

        app.folderBrowserPath = listing.wstring();
        app.hoveredFolderIndex = 7;
        app.folderBrowserScroll = 42.0f;
        populateFolderItems(app);

        check(app.hoveredFolderIndex == -1, "the hover index is reset");
        check(app.folderBrowserScroll == 0.0f, "the scroll offset is reset");

        // ".." then folders then files, folders and files each folded to lower case.
        // Written out as one expected sequence rather than as separate membership
        // tests: the ORDER is the behaviour here, and a set comparison would pass on
        // a comparator that had stopped sorting at all.
        //
        // notes.txt IS expected, and finding out why cost the first run of this test
        // two failures: isSupportedDocumentPath is not "markdown", it is markdown
        // plus mermaid plus PLAIN TEXT, and isPlainTextPath takes .txt, .json, .yaml,
        // .yml, .toml, .ini, .csv and .log. binary.bin is the one that must not
        // appear, and it is what makes this an assertion about filtering at all.
        const wchar_t* const expected[] = {L"..",       L"alpha",     L"Beta",
                                           L"apple.md", L"notes.txt", L"Zebra.md"};
        const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);
        if (app.folderItems.size() != expectedCount) {
            // Printed because the count alone does not say WHICH entry appeared or
            // vanished, and the first failure of this assertion was a wrong
            // expectation rather than a wrong listing.
            std::cerr << "      listing was:";
            for (const auto& item : app.folderItems) {
                std::cerr << " [" << std::string(item.name.begin(), item.name.end())
                          << (item.isDirectory ? "/]" : "]");
            }
            std::cerr << '\n';
        }
        check(app.folderItems.size() == expectedCount,
              "exactly the entries expected -- binary.bin is filtered by "
              "isSupportedDocumentPath, and nothing else appears");
        if (app.folderItems.size() == expectedCount) {
            bool ordered = true;
            for (size_t i = 0; i < expectedCount; i++) {
                if (app.folderItems[i].name != expected[i]) ordered = false;
            }
            check(ordered,
                  "\"..\", then directories, then files, each CASE-INSENSITIVELY "
                  "sorted -- \"alpha\" before \"Beta\" and \"apple.md\" before "
                  "\"Zebra.md\" is what fails if the fold is dropped");
            check(app.folderItems[1].isDirectory && app.folderItems[2].isDirectory,
                  "the directory entries are flagged as directories");
            check(!app.folderItems[3].isDirectory && !app.folderItems[4].isDirectory,
                  "and the file entries are not");
        }

        // The dot-file convention is POSIX's, and platform::isHiddenEntry says so at
        // length. Asserted only where it applies: on Windows a leading dot means
        // nothing, and setting the real attribute needs windows.h, which
        // platform_paths_tests already does.
#ifndef _WIN32
        writeFile(listing / L".hidden.md", "h\n");
        populateFolderItems(app);
        check(app.folderItems.size() == expectedCount,
              "a dot-file is omitted on POSIX, so the count is unchanged");
#endif

        // An unreadable or absent directory must come back EMPTY rather than throw.
        // populateFolderItems has no handler, so the error_code overloads in it are
        // the only thing standing between a missing folder and a crash.
        app.folderBrowserPath = (probeDir / L"no-such-folder").wstring();
        populateFolderItems(app);
        check(app.folderItems.size() == 1 && app.folderItems[0].name == L"..",
              "a directory that does not exist yields just \"..\" and does not throw");

        app.folderBrowserPath.clear();
        populateFolderItems(app);
        check(app.folderItems.empty(),
              "an empty browser path yields nothing at all, not even \"..\"");
    }

    fs::remove_all(probeDir, error);
    check(!error, "temporary probe directory is removed");

    // The count is printed on SUCCESS as well as failure. A test binary that prints
    // nothing when it passes is indistinguishable from one that was dropped from the
    // build, which this project has now been caught by twice.
    if (failures == 0) {
        std::cout << "File utils tests passed (" << checks << " assertions)\n";
        return 0;
    }
    std::cerr << failures << " failure(s) of " << checks << " assertions\n";
    return 1;
}

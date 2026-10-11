// settings.ini round-trip and the two things the port had to replace inside it:
// _stricmp (an MSVC extension) and GetSystemTimeAsFileTime (a Win32 call whose
// epoch is baked into the on-disk format).
//
// This target builds and runs on every platform, and that is the point. The ini
// reader and writer were unreachable off Windows for one reason -- settings.h
// included app.h -- and a comment claiming they are portable now would prove
// nothing. A non-Windows compiler accepting this and the assertions passing is
// the proof.
//
// Runs in portable mode via tests/run_portable_test.cmake, which copies the
// binary somewhere scratch and puts a settings.ini beside it. Without that the
// test would read and write the user's real configuration.

#include "settings.h"
#include "platform_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

void checkNear(float actual, float expected, const char* message) {
    check(actual > expected - 0.0001f && actual < expected + 0.0001f, message);
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    // --- The config home must be the scratch directory, not the real one.
    //
    // This is a guard and not a nicety: every assertion below writes settings.ini
    // through saveSettings, so if portable mode were not active the test would
    // overwrite the user's actual settings. Refuse to run rather than do that.
    const std::wstring iniPath = getSettingsPath();
    check(!iniPath.empty(), "getSettingsPath resolves a config home");
    if (iniPath.empty()) {
        std::cerr << "FAIL: no config home; refusing to continue\n";
        return 1;
    }
    const fs::path ini(iniPath);
    check(ini.filename() == fs::path("settings.ini"),
          "the settings path names settings.ini");
    check(ini.parent_path() == fs::current_path(),
          "portable mode is active: the config home is the scratch directory, "
          "so this test cannot touch the real configuration");
    if (ini.parent_path() != fs::current_path()) {
        std::cerr << "FAIL: config home is " << ini.parent_path().string()
                  << ", not " << fs::current_path().string()
                  << "; refusing to write to the real configuration\n";
        return 1;
    }
    // The separator came from the seam rather than a literal backslash. On Linux
    // the old spelling produced one file *named* "Tinta\settings.ini" instead of a
    // settings.ini inside a Tinta directory, which no existence check would catch.
    check(iniPath.find(L'\\') == std::wstring::npos ||
          platform::pathSeparator() == L'\\',
          "no stray backslash in the settings path on a platform that uses /");

    // --- documentPathsEqual: the _stricmp replacement.
    //
    // The expected answer differs by platform and both are correct, so the
    // assertion has to differ too. That asymmetry is the whole reason this sits
    // behind the seam instead of being a shared helper.
    check(platform::documentPathsEqual("/tmp/notes.md", "/tmp/notes.md"),
          "identical paths are equal");
    check(!platform::documentPathsEqual("/tmp/notes.md", "/tmp/other.md"),
          "different paths are not equal");
    check(!platform::documentPathsEqual("/tmp/notes.md", "/tmp/notes.mdx"),
          "a prefix is not equal to the longer path");
    check(platform::documentPathsEqual("", ""), "two empty paths are equal");
    check(!platform::documentPathsEqual("", "/tmp/notes.md"),
          "empty is not equal to a real path");
#ifdef _WIN32
    check(platform::documentPathsEqual("C:\\Docs\\Notes.MD", "c:\\docs\\notes.md"),
          "Windows folds case: one file, however it is spelled");
#else
    check(!platform::documentPathsEqual("/tmp/Notes.md", "/tmp/notes.md"),
          "Linux does not fold case: those are two different documents");
#endif
    // Non-ASCII bytes are compared verbatim on both platforms. This is the input
    // that would expose a locale-dependent fold, and it must not be folded: these
    // two UTF-8 strings are different characters, not one in two cases.
    check(!platform::documentPathsEqual("/tmp/\xC3\xA9.md", "/tmp/\xC3\x89.md"),
          "non-ASCII bytes are not case-folded");

    // --- fileTimeTicksNow: the GetSystemTimeAsFileTime replacement.
    const unsigned long long ticks = fileTimeTicksNow();
    // 2020-01-01 and 2200-01-01 in FILETIME ticks. A wrong epoch does not land
    // between them: a Unix epoch reads as the year 1601, and seconds instead of
    // 100ns intervals reads as 1601 too.
    check(ticks > 132223104000000000ULL, "the clock is after 2020");
    check(ticks < 189936288000000000ULL, "the clock is before 2200");
#ifdef _WIN32
    // The claim is that std::chrono and the Win32 call agree. Nothing but running
    // both and subtracting establishes that, so do exactly that. 10 seconds of
    // tolerance covers scheduling, not an epoch error -- an epoch error is 369
    // years and a unit error is a factor of ten million.
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER win32Now;
    win32Now.LowPart = ft.dwLowDateTime;
    win32Now.HighPart = ft.dwHighDateTime;
    const unsigned long long tolerance = 10ULL * 10000000ULL;
    const unsigned long long difference = win32Now.QuadPart > ticks
        ? win32Now.QuadPart - ticks : ticks - win32Now.QuadPart;
    check(difference < tolerance,
          "std::chrono agrees with GetSystemTimeAsFileTime, so existing "
          "settings.ini timestamps keep their meaning");
    if (difference >= tolerance) {
        std::cerr << "  chrono=" << ticks << " win32=" << win32Now.QuadPart
                  << " difference=" << difference << " ticks\n";
    }
#endif
    // Monotone across a real wait, which also confirms the unit: 100ns ticks
    // advance by ~200000 over 20ms. A seconds-based clock would advance by 0.
    const unsigned long long before = fileTimeTicksNow();
    // A steady_clock spin rather than std::this_thread::sleep_for, to keep this
    // target off <thread> and so off -pthread on Linux. 20ms, spun once.
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(20);
    while (std::chrono::steady_clock::now() < until) { /* spin */ }
    const unsigned long long after = fileTimeTicksNow();
    check(after > before, "the clock advances");

    // --- The round trip. Every field is moved off its default, so a loadSettings
    // that silently returned a default-constructed Settings would fail here
    // rather than pass by coincidence.
    Settings written;
    written.themeIndex = 3;
    written.zoomFactor = 1.25f;
    written.windowX = 120;
    written.windowY = 240;
    written.windowWidth = 1280;
    written.windowHeight = 900;
    written.windowMaximized = true;
    written.hasAskedFileAssociation = true;
    written.editorShowPreview = false;
    written.editorWordWrap = true;
    written.editorAssists = false;
    written.followSystemTheme = true;
    written.lightThemeIndex = 1;
    written.darkThemeIndex = 4;
    written.folderSearchEnabled = false;
    written.browserFocusPath = true;
    written.openInTabs = false;
    written.readingWidthPct = 80;
    written.zenWidthPct = 55;
    written.headingRules = false;
    written.tocOnLeft = true;
    written.tocPinned = true;
    written.browserPinned = true;
    written.language = "ja";
    written.keyProfile = "vim";
    written.pandocPath = "/usr/bin/pandoc";
    // A non-ASCII path, because the ini is byte-oriented UTF-8 and a wide-string
    // round trip through the stream would mangle it.
    const std::string unicodePath = "/tmp/\xE6\x97\xA5\xE6\x9C\xAC/notes.md";
    written.recentFiles.push_back({132223104000000000ULL, unicodePath});
    written.recentFiles.push_back({132223104000000001ULL, "/tmp/second.md"});
    written.readingPositions.push_back({unicodePath, 1234.5f, 1.5f});
    written.readingPositions.push_back({"/tmp/second.md", 42.0f, 0.0f});

    saveSettings(written);

    // Positive control on the instrument itself: the file has to exist and have
    // content. Without this, a saveSettings that wrote nothing at all and a
    // loadSettings that returned defaults could still agree with each other on a
    // subset of fields, and the comparisons below would be measuring nothing.
    std::error_code error;
    const auto size = fs::file_size(ini, error);
    check(!error, "settings.ini exists after saveSettings");
    check(!error && size > 0, "settings.ini is not empty");
    {
        std::ifstream raw(ini, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(raw)),
                         std::istreambuf_iterator<char>());
        check(body.find("[Settings]") != std::string::npos,
              "the written file has a [Settings] section");
        check(body.find(unicodePath) != std::string::npos,
              "the non-ASCII path reached the file byte for byte");
        check(body.find("[Keys]") != std::string::npos,
              "the written file documents the [Keys] section");
    }

    const Settings read = loadSettings();
    const Settings defaults;
    check(read.themeIndex != defaults.themeIndex,
          "the loaded settings are not just defaults");

    check(read.themeIndex == written.themeIndex, "themeIndex round-trips");
    checkNear(read.zoomFactor, written.zoomFactor, "zoomFactor round-trips");
    check(read.windowX == written.windowX, "windowX round-trips");
    check(read.windowY == written.windowY, "windowY round-trips");
    check(read.windowWidth == written.windowWidth, "windowWidth round-trips");
    check(read.windowHeight == written.windowHeight, "windowHeight round-trips");
    check(read.windowMaximized == written.windowMaximized,
          "windowMaximized round-trips");
    check(read.hasAskedFileAssociation == written.hasAskedFileAssociation,
          "hasAskedFileAssociation round-trips");
    check(read.editorShowPreview == written.editorShowPreview,
          "editorShowPreview round-trips");
    check(read.editorWordWrap == written.editorWordWrap,
          "editorWordWrap round-trips");
    check(read.editorAssists == written.editorAssists,
          "editorAssists round-trips");
    check(read.followSystemTheme == written.followSystemTheme,
          "followSystemTheme round-trips");
    check(read.lightThemeIndex == written.lightThemeIndex,
          "lightThemeIndex round-trips");
    check(read.darkThemeIndex == written.darkThemeIndex,
          "darkThemeIndex round-trips");
    check(read.folderSearchEnabled == written.folderSearchEnabled,
          "folderSearchEnabled round-trips");
    check(read.browserFocusPath == written.browserFocusPath,
          "browserFocusPath round-trips");
    check(read.openInTabs == written.openInTabs, "openInTabs round-trips");
    check(read.readingWidthPct == written.readingWidthPct,
          "readingWidthPct round-trips");
    check(read.zenWidthPct == written.zenWidthPct, "zenWidthPct round-trips");
    check(read.headingRules == written.headingRules, "headingRules round-trips");
    check(read.tocOnLeft == written.tocOnLeft, "tocOnLeft round-trips");
    check(read.tocPinned == written.tocPinned, "tocPinned round-trips");
    check(read.browserPinned == written.browserPinned, "browserPinned round-trips");
    check(read.language == written.language, "language round-trips");
    check(read.keyProfile == written.keyProfile, "keyProfile round-trips");
    check(read.pandocPath == written.pandocPath, "pandocPath round-trips");

    check(read.recentFiles.size() == 2, "both recent files round-trip");
    if (read.recentFiles.size() == 2) {
        check(read.recentFiles[0].path == unicodePath,
              "the non-ASCII recent path round-trips");
        check(read.recentFiles[0].when == 132223104000000000ULL,
              "the FILETIME tick count round-trips exactly, all 18 digits");
    }

    check(read.readingPositions.size() == 2, "both reading positions round-trip");
    if (read.readingPositions.size() == 2) {
        check(read.readingPositions[0].path == unicodePath,
              "the non-ASCII position path round-trips");
        checkNear(read.readingPositions[0].scrollY, 1234.5f,
                  "the scroll offset round-trips");
        checkNear(read.readingPositions[0].zoom, 1.5f,
                  "the per-document zoom round-trips");
    }

    // --- Lookup and insertion, which is where documentPathsEqual is actually used.
    checkNear(findReadingPosition(read, unicodePath), 1234.5f,
              "findReadingPosition finds the non-ASCII path");
    checkNear(findReadingZoom(read, unicodePath), 1.5f,
              "findReadingZoom finds the non-ASCII path");
    check(findReadingPosition(read, "/tmp/absent.md") < 0.0f,
          "an unknown path has no stored position");
    checkNear(findReadingZoom(read, "/tmp/absent.md"), 0.0f,
              "an unknown path has no stored zoom");

    Settings list;
    rememberReadingPosition(list, "/tmp/a.md", 10.0f, 2.0f);
    check(list.readingPositions.size() == 1, "the first position is inserted");
    // Re-remembering with zoom 0 keeps the known zoom rather than clearing it.
    rememberReadingPosition(list, "/tmp/a.md", 20.0f, 0.0f);
    check(list.readingPositions.size() == 1,
          "re-remembering the same path replaces rather than appends");
    checkNear(list.readingPositions[0].scrollY, 20.0f, "the new offset wins");
    checkNear(list.readingPositions[0].zoom, 2.0f,
              "a zero zoom keeps the previously known zoom");
    rememberReadingPosition(list, "", 99.0f, 1.0f);
    check(list.readingPositions.size() == 1, "an empty path is ignored");
    for (int i = 0; i < 60; i++) {
        rememberReadingPosition(list, "/tmp/f" + std::to_string(i) + ".md",
                                (float)i, 1.0f);
    }
    check(list.readingPositions.size() == 50, "the list is capped at 50");
    check(list.readingPositions[0].path == "/tmp/f59.md",
          "the most recent entry is at the head");

    if (failures == 0) {
        std::cout << "PASS: " << checks << " settings checks\n";
    } else {
        std::cerr << failures << " of " << checks << " settings checks failed\n";
    }
    return failures == 0 ? 0 : 1;
}

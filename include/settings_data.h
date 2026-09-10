#ifndef TINTA_SETTINGS_DATA_H
#define TINTA_SETTINGS_DATA_H

// The persisted settings record, with no platform dependency.
//
// This is the plain data half of what used to live in app.h. It is separated
// because Settings is pure configuration -- ints, bools, strings and vectors --
// and had no business being reachable only through a header that includes
// windows.h, d2d1.h, dwrite.h and wincodec.h. Anything that reads or writes
// settings.ini can now do so without the Windows GUI stack.
//
// NOTHING here may include windows.h, app.h, or any Direct2D/DirectWrite
// header. The ini reader/writer in settings.cpp depends on that, and so does
// every future non-Windows build.
//
// Deliberately NOT moved: CustomTheme, which sits next to Settings in app.h and
// looks equally plain. It embeds a D2DTheme, and D2DTheme is a wall of
// tinta::Color -- so it is Windows-only by its members, not by its own spelling.
// Freeing it means replacing tinta::Color tree-wide (442 sites), which is a
// separate decision.

#include <string>
#include <utility>
#include <vector>

// "let the window manager choose" for windowX/windowY.
//
// This value IS Win32's CW_USEDEFAULT, and app.h static_asserts that it still
// matches the SDK's macro, so the persisted meaning and the on-disk files are
// unchanged. It is restated here as a plain int because CW_USEDEFAULT is a macro
// from windows.h, and one macro was the only thing making this otherwise
// entirely platform-free struct unreachable off Windows.
inline constexpr int kWindowPositionUseDefault = static_cast<int>(0x80000000);

// Persistent settings
struct Settings {
    int themeIndex = 5;          // Default to Midnight
    float zoomFactor = 1.0f;
    int windowX = kWindowPositionUseDefault;
    int windowY = kWindowPositionUseDefault;
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowMaximized = false;
    bool hasAskedFileAssociation = false;
    bool editorShowPreview = true;
    bool editorWordWrap = false;
    // Auto theme: follow the Windows light/dark preference with a preferred
    // theme for each mode
    bool followSystemTheme = false;
    int lightThemeIndex = 0;   // Paper
    int darkThemeIndex = 5;    // Midnight
    // Search results from sibling markdown files in the search overlay
    bool folderSearchEnabled = true;
    // B opens the folder browser with the path box focused (#81)
    bool browserFocusPath = false;
    // Plain file launches join the existing window as a new tab (Win11
    // Notepad model); off restores one window per document
    bool openInTabs = true;
    // Last session's open tabs, restored on the next plain launch
    std::vector<std::string> sessionTabs;
    int sessionActive = 0;
    // Reading positions: most-recent-first, capped (#77). zoom = 0 means
    // "no per-document zoom": the document keeps the current zoom.
    struct ReadingPosition { std::string path; float scrollY; float zoom = 0.0f; };
    std::vector<ReadingPosition> readingPositions;
    // [Keys] overrides from settings.ini: action name -> key name (#77)
    std::vector<std::pair<std::string, std::string>> keyOverrides;
    // Shortcut profile: "windows" (default), "vim", or "custom" ([Keys])
    std::string keyProfile = "windows";
    // Reading column as a percentage of the window width (#82): 100 = full.
    // Windowed and fullscreen (zen) modes keep separate preferences.
    int readingWidthPct = 100;
    int zenWidthPct = 60;
    // Separator line under level 1 and 2 headings (#154): off renders
    // the heading text alone
    bool headingRules = true;
    // Table of contents panel side: false = right (default), true = left
    bool tocOnLeft = false;
    // Pinned side panels (#156): a pinned panel stops dismissing on
    // document clicks and hands the keyboard back to the document
    bool tocPinned = false;
    bool browserPinned = false;
    // UI language id ("auto" follows the Windows display language; else a
    // registry id like "en"/"zh"/"de" — see i18n.h). Persisted as a string
    // because languages.ini languages have no stable numeric index.
    // Credit: multilingual groundwork by wxh-777 (PR #84).
    std::string language = "auto";
    // Update check: portable builds ask GitHub for the latest release at
    // most once a day (Store installs update through the Store and never
    // check). dismissedUpdate remembers a version the user closed away.
    bool checkUpdates = true;
    std::string lastUpdateCheck;   // YYYY-MM-DD of the last completed check
    std::string dismissedUpdate;   // "X.Y.Z" the user dismissed
    // Recently opened files for the start page: most-recent-first,
    // capped at 10. when = FILETIME ticks of the last open.
    struct RecentFile { unsigned long long when = 0; std::string path; };
    std::vector<RecentFile> recentFiles;
    // Editor markdown assists (list continuation, Tab indent, Ctrl+B/I)
    // master switch
    bool editorAssists = true;
    // User-chosen pandoc executable ("" = auto-detect)
    std::string pandocPath;
};

#endif // TINTA_SETTINGS_DATA_H

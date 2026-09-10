// The settings.ini reader and writer. No windows.h, and that is now enforced by
// a non-Windows compiler building this file rather than by intent:
//
//   - the four functions that copied fields between App and Settings moved to
//     settings_app.cpp, since App is the Direct2D/Win32 window state;
//   - the file-association feature -- registry writes, MessageBoxW, the
//     ms-settings: deep link -- moved to settings_win32.cpp. That is a Windows
//     capability with no honest equivalent elsewhere, so it is a Windows-only
//     translation unit and deliberately NOT behind the platform seam.
//
// What is left is ini parsing and formatting, which needed none of it.
#include "settings.h"
#include "keymap.h"
#include "platform_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// keymap.h is included explicitly above. It used to arrive through settings.h ->
// app.h, and saveSettings needs KEY_ACTIONS to write the [Keys] section.

unsigned long long fileTimeTicksNow() {
    using namespace std::chrono;
    // 1601-01-01 to 1970-01-01 in 100ns ticks. Exact by definition rather than
    // by a leap-second table: FILETIME counts a uniform 100ns tick and does not
    // represent leap seconds at all.
    constexpr unsigned long long kUnixEpochInFileTimeTicks = 116444736000000000ULL;
    using FileTimeTicks = duration<long long, std::ratio<1, 10000000>>;
    const long long sinceUnixEpoch =
        duration_cast<FileTimeTicks>(system_clock::now().time_since_epoch()).count();
    return kUnixEpochInFileTimeTicks + static_cast<unsigned long long>(sinceUnixEpoch);
    // system_clock's epoch is only formally fixed to the Unix epoch in C++20.
    // Every implementation this tree can build with -- MSVC, libstdc++, libc++ --
    // already uses it, and settings_tests checks the result against
    // GetSystemTimeAsFileTime on Windows rather than trusting that.
}

// tintaConfigDir (portable mode #147) lives in config_dir.cpp so test
// targets can link it without this translation unit.

std::wstring getSettingsPath() {
    std::wstring dir = tintaConfigDir();
    if (dir.empty()) return L"";
    // Separator from the seam, not a literal L'\\'. A backslash is a legal
    // filename character on Linux, so the old spelling would not have failed
    // loudly there -- it would have created one file actually named
    // "...\settings.ini" inside the config directory.
    return dir + platform::pathSeparator() + L"settings.ini";
}

void saveSettings(const Settings& settings) {
    std::wstring path = getSettingsPath();
    if (path.empty()) return;

    // fs::path, not the wstring directly: constructing a stream from a wide
    // string is an MSVC extension that libstdc++ rejects. On Windows this is
    // free -- fs::path already stores a wstring natively.
    //
    // The path is named rather than written inline. `std::ofstream file(
    // std::filesystem::path(path))` is the most vexing parse: it declares a
    // *function* taking a path, and the error then surfaces at the first << with
    // a message that does not point here.
    const std::filesystem::path iniPath(path);
    std::ofstream file(iniPath);
    if (!file) return;

    file << "[Settings]\n";
    file << "themeIndex=" << settings.themeIndex << "\n";
    file << "zoomFactor=" << settings.zoomFactor << "\n";
    file << "windowX=" << settings.windowX << "\n";
    file << "windowY=" << settings.windowY << "\n";
    file << "windowWidth=" << settings.windowWidth << "\n";
    file << "windowHeight=" << settings.windowHeight << "\n";
    file << "windowMaximized=" << (settings.windowMaximized ? 1 : 0) << "\n";
    file << "hasAskedFileAssociation=" << (settings.hasAskedFileAssociation ? 1 : 0) << "\n";
    file << "editorShowPreview=" << (settings.editorShowPreview ? 1 : 0) << "\n";
    file << "editorWordWrap=" << (settings.editorWordWrap ? 1 : 0) << "\n";
    file << "editorAssists=" << (settings.editorAssists ? 1 : 0) << "\n";
    if (!settings.pandocPath.empty()) {
        file << "pandocPath=" << settings.pandocPath << "\n";
    }
    file << "followSystemTheme=" << (settings.followSystemTheme ? 1 : 0) << "\n";
    file << "lightThemeIndex=" << settings.lightThemeIndex << "\n";
    file << "darkThemeIndex=" << settings.darkThemeIndex << "\n";
    file << "folderSearchEnabled=" << (settings.folderSearchEnabled ? 1 : 0) << "\n";
    file << "browserFocusPath=" << (settings.browserFocusPath ? 1 : 0) << "\n";
    file << "openInTabs=" << (settings.openInTabs ? 1 : 0) << "\n";
    file << "readingWidthPct=" << settings.readingWidthPct << "\n";
    file << "zenWidthPct=" << settings.zenWidthPct << "\n";
    file << "headingRules=" << (settings.headingRules ? 1 : 0) << "\n";
    file << "tocOnLeft=" << (settings.tocOnLeft ? 1 : 0) << "\n";
    file << "tocPinned=" << (settings.tocPinned ? 1 : 0) << "\n";
    file << "browserPinned=" << (settings.browserPinned ? 1 : 0) << "\n";
    file << "language=" << settings.language << "\n";
    file << "keyProfile=" << settings.keyProfile << "\n";
    file << "checkUpdates=" << (settings.checkUpdates ? 1 : 0) << "\n";
    if (!settings.lastUpdateCheck.empty()) {
        file << "lastUpdateCheck=" << settings.lastUpdateCheck << "\n";
    }
    if (!settings.dismissedUpdate.empty()) {
        file << "dismissedUpdate=" << settings.dismissedUpdate << "\n";
    }

    // Remappable keys ("custom" profile), written with every save so the
    // section documents itself: change a value, restart Tinta
    file << "[Keys]\n";
    file << "; single letters/digits, Tab, Space, F1-F12, or one character\n";
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        std::string value = keyIniName({KEY_ACTIONS[i].defaultKey, KEY_ACTIONS[i].isChar});
        for (const auto& kv : settings.keyOverrides) {
            if (kv.first == KEY_ACTIONS[i].name) { value = kv.second; break; }
        }
        file << KEY_ACTIONS[i].name << "=" << value << "\n";
    }

    if (!settings.sessionTabs.empty()) {
        // Open tabs from the last session, restored on the next plain launch
        file << "[Session]\n";
        file << "sessionActive=" << settings.sessionActive << "\n";
        for (const auto& path : settings.sessionTabs) {
            file << "tab=" << path << "\n";
        }
    }

    if (!settings.recentFiles.empty()) {
        // Recently opened files for the start page, most recent first
        file << "[Recent]\n";
        for (const auto& recent : settings.recentFiles) {
            file << "recent=" << recent.when << "|" << recent.path << "\n";
        }
    }

    if (!settings.readingPositions.empty()) {
        file << "[Positions]\n";
        for (const auto& pos : settings.readingPositions) {
            // Windows paths cannot contain '|'; the second field is the
            // per-document zoom (0 = none)
            file << "pos=" << pos.scrollY << "|" << pos.zoom << "|"
                 << pos.path << "\n";
        }
    }
}

// applyKeymap moved to settings_app.cpp with the other three functions that
// touch App.

void rememberReadingPosition(Settings& settings, const std::string& path,
                             float scrollY, float zoom) {
    if (path.empty()) return;
    auto& list = settings.readingPositions;
    for (size_t i = 0; i < list.size(); i++) {
        // platform::documentPathsEqual, not _stricmp: _stricmp is an MSVC
        // extension, and whether two path spellings are one file is a platform
        // question the seam answers. See platform_paths.h.
        if (platform::documentPathsEqual(list[i].path, path)) {
            if (zoom <= 0.0f) zoom = list[i].zoom;  // keep a known zoom
            list.erase(list.begin() + i);
            break;
        }
    }
    list.insert(list.begin(), {path, scrollY, zoom});
    if (list.size() > 50) list.resize(50);
}

void persistReadingPosition(const std::string& path, float scrollY,
                            float zoom) {
    if (path.empty()) return;
    Settings settings = loadSettings();
    rememberReadingPosition(settings, path, scrollY, zoom);
    saveSettings(settings);
}

// persistEditorMode, persistOpenInTabs and persistThemeChoice moved to
// settings_app.cpp. They are one line of field copying each, and that one line
// is what tied this whole file to app.h and so to windows.h and Direct2D.

// Start page recents: every successful document open moves (or inserts)
// its path at the head of the [Recent] list
void persistRecentFile(const std::string& path) {
    if (path.empty()) return;
    Settings settings = loadSettings();
    auto& list = settings.recentFiles;
    for (size_t i = 0; i < list.size(); i++) {
        if (platform::documentPathsEqual(list[i].path, path)) {
            list.erase(list.begin() + i);
            break;
        }
    }
    list.insert(list.begin(), {fileTimeTicksNow(), path});
    if (list.size() > 10) list.resize(10);
    saveSettings(settings);
}

void clearRecentFiles() {
    Settings settings = loadSettings();
    settings.recentFiles.clear();
    saveSettings(settings);
}

float findReadingPosition(const Settings& settings, const std::string& path) {
    for (const auto& pos : settings.readingPositions) {
        if (platform::documentPathsEqual(pos.path, path)) return pos.scrollY;
    }
    return -1.0f;
}

float findReadingZoom(const Settings& settings, const std::string& path) {
    for (const auto& pos : settings.readingPositions) {
        if (platform::documentPathsEqual(pos.path, path)) return pos.zoom;
    }
    return 0.0f;
}

Settings loadSettings() {
    Settings settings;
    std::wstring path = getSettingsPath();
    if (path.empty()) return settings;

    // Named fs::path for the same two reasons as in saveSettings above: a wide
    // string here is MSVC-only, and the inline spelling is the most vexing parse.
    const std::filesystem::path iniPath(path);
    std::ifstream file(iniPath);
    if (!file) return settings;

    bool sawKeyProfile = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "themeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.themeIndex = idx;  // clamped at apply via themeCount()
        } else if (key == "zoomFactor") {
            float z = std::stof(value);
            if (z >= 0.5f && z <= 3.0f) settings.zoomFactor = z;
        } else if (key == "windowX") {
            settings.windowX = std::stoi(value);
        } else if (key == "windowY") {
            settings.windowY = std::stoi(value);
        } else if (key == "windowWidth") {
            int w = std::stoi(value);
            if (w >= 200) settings.windowWidth = w;
        } else if (key == "windowHeight") {
            int h = std::stoi(value);
            if (h >= 200) settings.windowHeight = h;
        } else if (key == "readingWidthPct") {
            int w = std::stoi(value);
            if (w >= 30 && w <= 100) settings.readingWidthPct = w;
        } else if (key == "zenWidthPct") {
            int w = std::stoi(value);
            if (w >= 30 && w <= 100) settings.zenWidthPct = w;
        } else if (key == "headingRules") {
            settings.headingRules = (value == "1");
        } else if (key == "tocOnLeft") {
            settings.tocOnLeft = (value == "1");
        } else if (key == "tocPinned") {
            settings.tocPinned = (value == "1");
        } else if (key == "browserPinned") {
            settings.browserPinned = (value == "1");
        } else if (key == "language") {
            if (!value.empty()) settings.language = value;  // "auto" or an id
        } else if (key == "keyProfile") {
            if (value == "custom" || keyProfileIndexById(value) >= 0) {
                settings.keyProfile = value;
                sawKeyProfile = true;
            }
        } else if (key == "languageIndex") {
            // Legacy numeric form from the first i18n build
            int idx = std::stoi(value);
            const char* ids[] = {"en", "zh", "ja", "ko"};
            settings.language = (idx >= 0 && idx < 4) ? ids[idx] : "auto";
        } else if (key == "folderSearchEnabled") {
            settings.folderSearchEnabled = (value == "1");
        } else if (key == "browserFocusPath") {
            settings.browserFocusPath = (value == "1");
        } else if (key == "openInTabs") {
            settings.openInTabs = (value == "1");
        } else if (key == "checkUpdates") {
            settings.checkUpdates = (value == "1");
        } else if (key == "lastUpdateCheck") {
            settings.lastUpdateCheck = value;
        } else if (key == "dismissedUpdate") {
            settings.dismissedUpdate = value;
        } else if (key == "followSystemTheme") {
            settings.followSystemTheme = (value == "1");
        } else if (key == "lightThemeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.lightThemeIndex = idx;
        } else if (key == "darkThemeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.darkThemeIndex = idx;
        } else if (key == "windowMaximized") {
            settings.windowMaximized = (value == "1");
        } else if (key == "hasAskedFileAssociation") {
            settings.hasAskedFileAssociation = (value == "1");
        } else if (key == "editorShowPreview") {
            settings.editorShowPreview = (value == "1");
        } else if (key == "editorWordWrap") {
            settings.editorWordWrap = (value == "1");
        } else if (key == "editorAssists") {
            settings.editorAssists = (value == "1");
        } else if (key == "pandocPath") {
            settings.pandocPath = value;
        } else if (key == "sessionActive") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.sessionActive = idx;
        } else if (key == "tab") {
            if (!value.empty() && settings.sessionTabs.size() < 64) {
                settings.sessionTabs.push_back(value);
            }
        } else if (key == "recent") {
            // when(FILETIME)|path, most recent first
            size_t sep = value.find('|');
            if (sep != std::string::npos && sep + 1 < value.size() &&
                settings.recentFiles.size() < 10) {
                try {
                    unsigned long long when =
                        std::stoull(value.substr(0, sep));
                    std::string recentPath = value.substr(sep + 1);
                    if (!recentPath.empty()) {
                        settings.recentFiles.push_back({when, recentPath});
                    }
                } catch (...) {}
            }
        } else if (key == "pos") {
            // New format: scrollY|zoom|path; legacy: scrollY|path
            size_t sep = value.find('|');
            if (sep != std::string::npos && sep + 1 < value.size()) {
                try {
                    float y = std::stof(value.substr(0, sep));
                    float zoom = 0.0f;
                    std::string path = value.substr(sep + 1);
                    size_t sep2 = path.find('|');
                    if (sep2 != std::string::npos && sep2 + 1 < path.size()) {
                        zoom = std::stof(path.substr(0, sep2));
                        path = path.substr(sep2 + 1);
                    }
                    if (y >= 0.0f && !path.empty()) {
                        settings.readingPositions.push_back({path, y, zoom});
                    }
                } catch (...) {}
            }
        } else {
            for (int i = 0; i < KEY_ACTION_COUNT; i++) {
                if (key == KEY_ACTIONS[i].name) {
                    settings.keyOverrides.push_back({key, value});
                    break;
                }
            }
        }
    }
    if (settings.readingPositions.size() > 50) settings.readingPositions.resize(50);

    // Migration from pre-profile builds (#77-era [Keys] only): users who
    // had customized any binding keep exactly what they had via the custom
    // profile; everyone else gets the Windows profile (':' and '?' stay
    // hardwired, so nothing they know stops working)
    if (!sawKeyProfile) {
        bool customized = false;
        for (const auto& kv : settings.keyOverrides) {
            for (int i = 0; i < KEY_ACTION_COUNT; i++) {
                if (kv.first == KEY_ACTIONS[i].name &&
                    kv.second != keyIniName({KEY_ACTIONS[i].defaultKey, KEY_ACTIONS[i].isChar})) {
                    customized = true;
                    break;
                }
            }
            if (customized) break;
        }
        settings.keyProfile = customized ? "custom" : "windows";
    }
    return settings;
}

// The file-association feature -- registerFileAssociation,
// openDefaultAppsSettings, askAndRegisterFileAssociation and their two static
// helpers -- continues in settings_win32.cpp.

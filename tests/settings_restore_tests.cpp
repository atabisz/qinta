// Settings and session restore on qinta: what a launch reads back from settings.ini, what a close
// writes, and which session the next launch reopens. Through the same functions main() and the
// close save call (settings.h, qt_launch.h), not copies of them, so a field dropped from either
// direction fails here.
//
// Until 2026-10-11 qinta's launch read eleven fields and its close save wrote six, so zoom,
// reading width, word wrap, the pins, the language and the session reset on every launch. Each
// value below differs from App's default, and that is checked first: a field that was never
// restored would otherwise read back equal by sitting at its default, and pass.
//
// Runs only in the isolated portable folder tests/run_portable_test.cmake stages, which is what
// keeps every saveSettings() here out of the user's real configuration.
#include <QApplication>
#include <QCoreApplication>

#include "app.h"
#include "i18n.h"
#include "launch_options.h"
#include "platform_paths.h"
#include "qt_init.h"
#include "qt_launch.h"
#include "settings.h"
#include "text_utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using tinta::LaunchOptions;

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void touch(const std::filesystem::path& path) {
    std::ofstream(path, std::ios::binary) << "# restore fixture\n";
}

// A preference block that differs from App's default in every field. The language is the first
// one that is not the system's, so "auto" and the saved choice give different answers.
void setNonDefault(App& app, int language) {
    app.zoomFactor = 1.5f;
    app.editorShowPreview = !app.editorShowPreview;
    app.editorWordWrap = !app.editorWordWrap;
    app.editorAssists = !app.editorAssists;
    app.folderSearchEnabled = !app.folderSearchEnabled;
    app.browserFocusPath = !app.browserFocusPath;
    app.readingWidthPct = 63;
    app.zenWidthPct = 47;
    app.headingRules = !app.headingRules;
    app.tocOnLeft = !app.tocOnLeft;
    app.tocPinned = !app.tocPinned;
    app.browserPinned = !app.browserPinned;
    app.languageSetting = language;
}

void checkPreferencesRoundTrip(const std::filesystem::path& dir) {
    const auto defaults = std::make_unique<App>();
    const int system = detectSystemLanguage();
    const int language = system == 0 ? 1 : 0;

    auto saved = std::make_unique<App>();
    setNonDefault(*saved, language);
    // The discriminator: every field this test reads back starts different from the default.
    check(saved->zoomFactor != defaults->zoomFactor &&
              saved->editorShowPreview != defaults->editorShowPreview &&
              saved->editorWordWrap != defaults->editorWordWrap &&
              saved->editorAssists != defaults->editorAssists &&
              saved->folderSearchEnabled != defaults->folderSearchEnabled &&
              saved->browserFocusPath != defaults->browserFocusPath &&
              saved->readingWidthPct != defaults->readingWidthPct &&
              saved->zenWidthPct != defaults->zenWidthPct &&
              saved->headingRules != defaults->headingRules &&
              saved->tocOnLeft != defaults->tocOnLeft && saved->tocPinned != defaults->tocPinned &&
              saved->browserPinned != defaults->browserPinned &&
              saved->languageSetting != defaults->languageSetting,
          "CONTROL: every preference under test starts away from App's default");

    const std::string doc = (dir / "reading.md").string();
    touch(doc);
    saved->currentFile = doc;
    saved->scrollY = 321.0f;

    Settings out = loadSettings();
    storePreferences(out, *saved);
    saveSettings(out);

    const Settings in = loadSettings();
    auto restored = std::make_unique<App>();
    applySavedPreferences(*restored, in);
    check(restored->zoomFactor == 1.5f, "zoom survives a close and a launch");
    check(restored->editorShowPreview == saved->editorShowPreview, "editor preview survives");
    check(restored->editorWordWrap == saved->editorWordWrap, "word wrap survives");
    check(restored->editorAssists == saved->editorAssists, "editor assists survive");
    check(restored->folderSearchEnabled == saved->folderSearchEnabled, "folder search survives");
    check(restored->browserFocusPath == saved->browserFocusPath, "browser focus survives");
    check(restored->readingWidthPct == 63, "reading width survives");
    check(restored->zenWidthPct == 47, "zen width survives");
    check(restored->headingRules == saved->headingRules, "heading rules survive");
    check(restored->tocOnLeft == saved->tocOnLeft, "table-of-contents side survives");
    check(restored->tocPinned == saved->tocPinned, "table-of-contents pin survives");
    check(restored->browserPinned == saved->browserPinned, "browser pin survives");
    check(restored->languageSetting == language, "a chosen UI language survives");
    check(restored->currentLanguageIndex == language, "and is the language in force at launch");
    check(std::fabs(findReadingPosition(in, doc) - 321.0f) < 0.5f,
          "the open document's reading position is saved at close");

    // "auto" is not a language: the launch takes the system's.
    Settings automatic = in;
    automatic.language = "auto";
    auto followsSystem = std::make_unique<App>();
    applySavedPreferences(*followsSystem, automatic);
    check(followsSystem->languageSetting == -1 &&
              followsSystem->currentLanguageIndex == system,
          "language \"auto\" launches in the system language");
}

// A settings.ini written before the update check was removed (2026-10-11) still carries its
// three keys. It must load with the rest intact, and the next save must drop them.
void checkLegacyUpdateKeys(const std::filesystem::path& dir) {
    {
        std::ofstream ini(dir / "settings.ini", std::ios::binary);
        ini << "[Settings]\nreadingWidthPct=58\ncheckUpdates=0\nlastUpdateCheck=2026-09-10\n"
               "dismissedUpdate=3.6.7\nzenWidthPct=41\n";
    }
    const Settings legacy = loadSettings();
    check(legacy.readingWidthPct == 58 && legacy.zenWidthPct == 41,
          "a settings.ini with the retired update keys loads, the fields around them intact");
    saveSettings(legacy);
    const std::string rewritten = read(dir / "settings.ini");
    check(rewritten.find("readingWidthPct=58") != std::string::npos,
          "CONTROL: the rewritten file was read, and it holds a field that was in the old one");
    check(rewritten.find("checkUpdates") == std::string::npos &&
              rewritten.find("lastUpdateCheck") == std::string::npos &&
              rewritten.find("dismissedUpdate") == std::string::npos,
          "and the next save leaves the retired keys out");
}

void checkSessionWrite(const std::filesystem::path& dir) {
    const std::string a = (dir / "a.md").string(), b = (dir / "b.md").string(),
                      c = (dir / "c.md").string();
    for (const auto& p : {a, b, c}) touch(p);

    auto owner = std::make_unique<App>();
    owner->tabs.resize(3);
    owner->tabs[0].path = a;
    owner->tabs[1].path = "";  // an untitled buffer: no path to restore
    owner->tabs[2].path = c;
    owner->activeTab = 2;
    owner->currentFile = c;
    Settings s = loadSettings();
    storePreferences(s, *owner);
    saveSettings(s);
    Settings written = loadSettings();
    check(written.sessionTabs == std::vector<std::string>{a, c},
          "the session holds the open tabs with paths, untitled ones dropped");
    check(written.sessionActive == 1, "and the active tab's slot follows the dropped one");

    // A window that does not own the session leaves it as it is on disk.
    auto satellite = std::make_unique<App>();
    satellite->sessionOwner = false;
    satellite->tabs.resize(1);
    satellite->tabs[0].path = b;
    satellite->activeTab = 0;
    satellite->currentFile = b;
    Settings t = loadSettings();
    storePreferences(t, *satellite);
    saveSettings(t);
    check(loadSettings().sessionTabs == std::vector<std::string>{a, c},
          "a quick note or dragged-out window does not overwrite the session");

    // One document and no tab operation yet: qinta's tab strip is lazy, and that document is
    // still the session.
    auto single = std::make_unique<App>();
    single->currentFile = b;
    Settings u = loadSettings();
    storePreferences(u, *single);
    saveSettings(u);
    check(loadSettings().sessionTabs == std::vector<std::string>{b},
          "a window that never opened a tab strip saves its one document as the session");
}

void checkSessionChoice(const std::filesystem::path& dir) {
    const std::string a = (dir / "a.md").string(), gone = (dir / "gone.md").string(),
                      c = (dir / "c.md").string();
    Settings s;
    s.openInTabs = true;
    s.sessionTabs = {a, gone, c};
    s.sessionActive = 2;
    LaunchOptions plain;
    int active = -1;
    const auto paths = tinta::sessionToRestore(plain, s, active);
    check(paths == std::vector<std::string>{a, c}, "a plain launch reopens the session, minus "
                                                   "files that no longer exist");
    check(active == 1, "and the active tab follows its path through the filtering");

    auto none = [&](LaunchOptions o, Settings st, const char* why) {
        int unused = 0;
        check(tinta::sessionToRestore(o, st, unused).empty(), why);
    };
    LaunchOptions withFile;
    withFile.inputFile = a;
    none(withFile, s, "a file argument opens that file, not the session");
    LaunchOptions note;
    note.quickNote = true;
    none(note, s, "a quick note does not reopen the session");
    LaunchOptions cascade;
    cascade.cascade = true;
    none(cascade, s, "a cascaded new window does not reopen the session");
    LaunchOptions edit;
    edit.startInEditMode = true;
    none(edit, s, "an --edit launch does not reopen the session");
    LaunchOptions harness;
    harness.printPagesDir = (dir / "pages").string();
    none(harness, s, "a harness run does not reopen the session");
    Settings oneWindow = s;
    oneWindow.openInTabs = false;
    none(plain, oneWindow, "with Open files in tabs off, a window holds one document");

    check(tinta::ownsSession(plain), "a plain launch owns the session");
    check(!tinta::ownsSession(note) && !tinta::ownsSession(cascade),
          "a quick note and a cascaded window do not");
}

void checkLaunchDocument(App& app, const std::filesystem::path& dir) {
    const std::string doc = (dir / "zoomed.md").string();
    touch(doc);
    Settings s = loadSettings();
    rememberReadingPosition(s, doc, 456.0f, 1.75f);
    s.recentFiles.clear();
    saveSettings(s);

    LaunchOptions named;
    named.inputFile = doc;
    app.currentFile = doc;
    app.pendingScrollRestore = 0.0f;
    const float before = app.appliedZoomFactor;
    tinta::restoreLaunchDocument(app, loadSettings(), named, true);
    check(std::fabs(app.pendingScrollRestore - 456.0f) < 0.5f,
          "a launched document resumes at its saved position");
    check(std::fabs(app.zoomFactor - 1.75f) < 0.01f, "and at its saved zoom");
    check(before != 1.75f && std::fabs(app.appliedZoomFactor - 1.75f) < 0.01f,
          "and the text formats are rebuilt at that zoom, not left at the one they were built at");
    const Settings after = loadSettings();
    check(!after.recentFiles.empty() &&
              platform::documentPathsEqual(after.recentFiles.front().path, doc),
          "a file named on the command line goes to the head of the recent files");

    // --edit opens at the top, and a harness run records nothing.
    Settings cleared = loadSettings();
    cleared.recentFiles.clear();
    saveSettings(cleared);
    LaunchOptions edit = named;
    edit.startInEditMode = true;
    edit.printPagesDir = (dir / "pages").string();
    app.pendingScrollRestore = 0.0f;
    tinta::restoreLaunchDocument(app, loadSettings(), edit, true);
    check(app.pendingScrollRestore == 0.0f, "an --edit launch opens at the top");
    check(loadSettings().recentFiles.empty(), "a harness run does not touch the recent files");
}
}  // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    QApplication qapp(argc, argv);
    // The same wrapper and the same refusal as tests/theme_tests.cpp: this test saves settings,
    // so it runs only where those saves land in its own folder.
    const auto dir =
        fs::path(QCoreApplication::applicationFilePath().toStdWString()).parent_path();
    const auto marker = read(dir / "settings.ini");
    if (argc != 2 || std::string(argv[1]) != "--portable-test" ||
        !fs::equivalent(dir, fs::current_path()) ||
        (marker != "; Isolated native theme test configuration\n" &&
         marker != "; Isolated native theme test configuration\r\n")) {
        std::cerr << "Run this test through CTest's isolated portable wrapper\n";
        return 2;
    }
    check(fs::equivalent(tintaConfigDir(), dir), "settings land only in the portable test folder");

    checkPreferencesRoundTrip(dir);
    checkLegacyUpdateKeys(dir);
    checkSessionWrite(dir);
    checkSessionChoice(dir);
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        installQtBackends(app);
        if (!app.textFactory) return 2;
        checkLaunchDocument(app, dir);
        releaseQtBackends(app);
    }
    std::cout << "settings restore: " << failures << " failures\n";
    return failures ? 1 : 0;
}

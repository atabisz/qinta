// The settings functions that touch App: applyKeymap, the three persist-on-change ones (editor
// mode, open-in-tabs, theme), storeThemeChoice
// (the field copy persistThemeChoice and the close save share), and applySavedPreferences and
// storePreferences, the launch and close halves of every other field.
//
// They are here rather than in settings.cpp because that single line of field
// copying each was what kept the entire ini reader and writer, 380 lines of string
// parsing, tied to app.h. Nothing else moved with them: the split is along "does
// this need App", not along features, which is why applyKeymap sits next to
// persistThemeChoice despite having nothing else in common.
//
// THE ORIGINAL REASON GIVEN HERE IS NOW FALSE, and is corrected rather than
// deleted because the correction is the useful part. It read: "App is the live
// window state -- Direct2D render targets, DirectWrite layouts, HWNDs -- so a file
// that mentions it can never compile off Windows." App no longer names a backend;
// its wrapper storage is pointers to the abstract seam types (include/seated.h),
// and this file compiles clean with g++ today -- measured, 0 errors.
//
// IT WAS WINDOWS-ONLY IN THE BUILD, DELIBERATELY, UNTIL 2026-09-24, for a reason that
// had nothing to do with the sentence above. scripts/probe-portable-linux.sh asserted
// that settings_app.cpp.o appeared in no non-Windows build, as a stand-in for the real
// property: that the portable half had not grown App or a registry call back. "Compiling
// this file off Windows would buy four lines of field copying and cost a working guard."
//
// The price changed, not the argument. A link fixpoint over the manifest measured that
// src/input.cpp needs all four functions here, and input.cpp gates fourteen compile-only
// files (README, 2026-09-24). So the guard was re-pointed at what it stood in for: the
// probe now reads settings.cpp's transitive header list with g++ -MM and requires no
// include/app.h in it, with THIS file as the control that proves the instrument can see
// app.h. The guard survives; only its proxy went.
//
// So the premise expired, the conclusion survived one more pass, and then the conclusion's
// cost was measured and it lost. A gate whose stated reason has gone stale is not
// automatically a gate to remove -- it has to be re-argued on its own terms -- and a gate
// that still holds can be replaced by a better instrument for the same property.
#include "app.h"
#include "settings.h"
#include "keymap.h"
#include "i18n.h"
#include "text_utils.h"

void applyKeymap(App& app, const Settings& settings) {
    resolveKeymap(app.keymap, settings.keyProfile, settings.keyOverrides);
    app.keyProfile = settings.keyProfile;
}

// The editor assists toggle persists immediately so fresh windows pick
// it up without waiting for this one's exit save
void persistEditorMode(const App& app) {
    Settings settings = loadSettings();
    settings.editorAssists = app.editorAssists;
    saveSettings(settings);
}

// "Open files in tabs" persists the moment it is toggled: the join-or-
// spawn decision is made by the NEXT launched process reading
// settings.ini, so an exit-time save would leave every Explorer open on
// the old mode until all windows closed (#147 follow-up). The ini is the
// single source of truth for this flag - the exit save leaves it alone.
void persistOpenInTabs(const App& app) {
    Settings settings = loadSettings();
    settings.openInTabs = app.openInTabs;
    saveSettings(settings);
}

// A theme change persists immediately: windows spawned afterwards
// (quick notes, drag-outs) read settings.ini at startup and would
// otherwise come up in the look from the previous session
void persistThemeChoice(const App& app) {
    Settings settings = loadSettings();
    storeThemeChoice(settings, app);
    saveSettings(settings);
}

void storeThemeChoice(Settings& settings, const App& app) {
    settings.themeIndex = app.currentThemeIndex;
    settings.followSystemTheme = app.followSystemTheme;
    settings.lightThemeIndex = app.lightThemeIndex;
    settings.darkThemeIndex = app.darkThemeIndex;
}

// WinMain's block after loadSettings(), field for field, minus the theme. Until 2026-10-11
// qinta's launch copied 11 fields and the rest stayed at App's defaults, so zoom, reading
// width, word wrap, the pins, the update-check state and the UI language reset every launch.
void applySavedPreferences(App& app, const Settings& settings) {
    app.updateCheckEnabled = settings.checkUpdates;
    app.updateLastCheck = settings.lastUpdateCheck;
    app.updateDismissedVersion = settings.dismissedUpdate;
    app.folderSearchEnabled = settings.folderSearchEnabled;
    app.browserFocusPath = settings.browserFocusPath;
    app.openInTabs = settings.openInTabs;
    app.editorAssists = settings.editorAssists;
    app.pandocUserPath = toWide(settings.pandocPath);
    app.zoomFactor = settings.zoomFactor;
    app.editorShowPreview = settings.editorShowPreview;
    app.editorWordWrap = settings.editorWordWrap;
    app.readingWidthPct = settings.readingWidthPct;
    app.zenWidthPct = settings.zenWidthPct;
    app.headingRules = settings.headingRules;
    app.tocOnLeft = settings.tocOnLeft;
    app.tocPinned = settings.tocPinned;
    app.browserPinned = settings.browserPinned;
    app.languageSetting = settings.language == "auto"
        ? -1 : languageIndexById(settings.language);
    app.currentLanguageIndex = app.languageSetting >= 0
        ? app.languageSetting : detectSystemLanguage();
    app.keyOverrides = settings.keyOverrides;
    applyKeymap(app, settings);
}

// WinMain's WM_CLOSE block, field for field, minus the window placement.
void storePreferences(Settings& settings, const App& app) {
    storeThemeChoice(settings, app);
    settings.zoomFactor = app.zoomFactor;
    settings.editorShowPreview = app.editorShowPreview;
    settings.editorWordWrap = app.editorWordWrap;
    settings.editorAssists = app.editorAssists;
    settings.checkUpdates = app.updateCheckEnabled;
    settings.lastUpdateCheck = app.updateLastCheck;
    settings.dismissedUpdate = app.updateDismissedVersion;
    settings.folderSearchEnabled = app.folderSearchEnabled;
    settings.browserFocusPath = app.browserFocusPath;
    // The open tabs, for the next launch to reopen. Untitled buffers have no path to restore.
    // Only the session-owning window writes them: a quick note or a dragged-out window keeps
    // what is on disk, so its close cannot replace a newer session with its own few tabs.
    if (app.sessionOwner) {
        settings.sessionTabs.clear();
        settings.sessionActive = 0;
        for (size_t i = 0; i < app.tabs.size(); i++) {
            const std::string& path =
                (int)i == app.activeTab ? app.currentFile : app.tabs[i].path;
            if (path.empty()) continue;
            if ((int)i == app.activeTab) settings.sessionActive = (int)settings.sessionTabs.size();
            settings.sessionTabs.push_back(path);
        }
        // No tab strip yet (one document, never a tab operation): that document is the session.
        if (app.tabs.empty() && !app.currentFile.empty()) settings.sessionTabs.push_back(app.currentFile);
    }
    settings.readingWidthPct = app.readingWidthPct;
    settings.zenWidthPct = app.zenWidthPct;
    settings.headingRules = app.headingRules;
    settings.tocOnLeft = app.tocOnLeft;
    settings.tocPinned = app.tocPinned;
    settings.browserPinned = app.browserPinned;
    settings.language = app.languageSetting >= 0 ? languageIdAt(app.languageSetting) : "auto";
    settings.keyProfile = app.keyProfile;
    settings.keyOverrides = app.keyOverrides;
    if (!app.currentFile.empty()) {
        rememberReadingPosition(settings, app.currentFile, app.scrollY, app.zoomFactor);
    }
}

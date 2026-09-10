// The four settings functions that touch App.
//
// They are here rather than in settings.cpp because App is the live window state
// -- Direct2D render targets, DirectWrite layouts, HWNDs -- so a file that
// mentions it can never compile off Windows. These four are a line of field
// copying each, and that single line was what kept the entire ini reader and
// writer, 380 lines of string parsing, tied to windows.h and Direct2D.
//
// Nothing else moved with them. The split is along "does this need App", not
// along features, which is why applyKeymap sits next to persistThemeChoice
// despite having nothing else in common.
#include "app.h"
#include "settings.h"
#include "keymap.h"

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
    settings.themeIndex = app.currentThemeIndex;
    settings.followSystemTheme = app.followSystemTheme;
    settings.lightThemeIndex = app.lightThemeIndex;
    settings.darkThemeIndex = app.darkThemeIndex;
    saveSettings(settings);
}

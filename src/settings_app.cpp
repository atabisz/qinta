// The four settings functions that touch App.
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
// IT IS STILL WINDOWS-ONLY IN THE BUILD, DELIBERATELY, and for a reason that has
// nothing to do with the sentence above. scripts/probe-portable-linux.sh asserts
// that settings_app.cpp.o appears in no non-Windows build, and that assertion is
// what gives the settings.cpp pass its meaning: if a future edit put App or a
// registry call back into the portable half, or wired these four into a portable
// target, the probe is where it shows. Compiling this file off Windows would buy
// four lines of field copying and cost a working guard.
//
// So the premise expired and the conclusion survived, which is why they are worth
// separating. A gate whose stated reason has gone stale is not automatically a gate
// to remove -- it has to be re-argued on its own terms, and this one still holds.
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

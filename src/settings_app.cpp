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

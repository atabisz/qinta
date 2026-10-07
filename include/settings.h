#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

// settings_data.h rather than app.h. This header declares the settings.ini
// reader and writer, and none of that needs the Windows GUI stack -- it needed
// app.h only because the Settings struct used to be defined there.
//
// App is forward declared instead. The four functions below that touch it take
// it by reference, which an incomplete type satisfies, so callers who genuinely
// use App now include app.h themselves. That is the point: including this header
// no longer drags in windows.h, d2d1.h, dwrite.h and wincodec.h.
#include "settings_data.h"
#include "config_dir.h"

#include <string>

struct App;

std::wstring getSettingsPath();
void saveSettings(const Settings& settings);
Settings loadSettings();

// Reading position memory (#77): most-recent-first list capped in remember.
// The persist/lookup helpers load and save settings.ini themselves.
void rememberReadingPosition(Settings& settings, const std::string& path,
                             float scrollY, float zoom = 0.0f);
void persistReadingPosition(const std::string& path, float scrollY,
                            float zoom = 0.0f);
float findReadingPosition(const Settings& settings, const std::string& path);
// Per-document zoom, or 0 when the document has none stored
float findReadingZoom(const Settings& settings, const std::string& path);

// Start page recents ([Recent] section): record an open, or clear the list
void persistRecentFile(const std::string& path);
void clearRecentFiles();

// Now, as Windows FILETIME ticks: 100ns intervals since 1601-01-01 UTC.
//
// That epoch is not a style choice. It is what the [Recent] timestamps in
// settings.ini already hold, and the start page decodes them with
// FileTimeToLocalFileTime -- so switching to a Unix epoch here would silently
// redate every existing entry by 369 years. Computed from std::chrono so this
// file needs no windows.h; declared here rather than kept static so a test can
// check it against GetSystemTimeAsFileTime on Windows instead of assuming.
unsigned long long fileTimeTicksNow();

// Theme changes persist immediately so new windows spawn in the same look
void persistThemeChoice(const App& app);
// The four theme fields persistThemeChoice writes, copied onto a loaded Settings -- shared
// with the Qt arm's close save (qt_launch.cpp) so the two cannot drift apart.
void storeThemeChoice(Settings& settings, const App& app);
// Editor mode pill choice (and the assists switch) persist immediately
void persistEditorMode(const App& app);
void persistOpenInTabs(const App& app);

// tintaConfigDir() and configFilePathForShell() moved to config_dir.h, which is
// included above so callers of this header are unaffected. They left because
// they need no part of app.h, and a file that only resolves a path should not be
// tied to windows.h and Direct2D.

// Resolve the selected profile and custom [Keys] bindings.
void applyKeymap(App& app, const Settings& settings);
bool registerFileAssociation();
// `/register`: registerFileAssociation, then a confirmation or failure box, then Default apps.
// Windows only (src/settings_win32.cpp).
bool registerFileAssociationAndReport(const Settings& settings);
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H

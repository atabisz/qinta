#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

#include "app.h"
#include "config_dir.h"

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

// Theme changes persist immediately so new windows spawn in the same look
void persistThemeChoice(const App& app);
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
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H

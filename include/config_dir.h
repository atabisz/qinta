#ifndef TINTA_CONFIG_DIR_H
#define TINTA_CONFIG_DIR_H

#include <string>

// Split out of settings.h so that config_dir.cpp can compile without app.h --
// and so without windows.h, d2d1.h and dwrite.h, which app.h pulls in. These
// two declarations need none of that. settings.h includes this header, so every
// existing caller keeps compiling unchanged.

// The configuration home: the exe's folder in portable mode (a settings.ini
// beside tinta.exe opts in, #147), otherwise the per-user configuration
// directory. Resolved once per run and cached.
std::wstring tintaConfigDir();

// Fix the configuration home for the rest of the run, replacing the cached answer.
// For tests that save settings: an APPDATA redirect is not enough, because the Win32
// path backend asks SHGetFolderPathW, which ignores the environment variable -- so a
// test that only redirected it would write the user's real settings.ini on Windows.
void pinTintaConfigDir(const std::wstring& dir);

// Resolve the actual file before handing it to an external editor, which does
// not share a Store/MSIX process's virtualized AppData view. That virtualization
// is a Windows mechanism with no counterpart elsewhere, so on other platforms
// the caller's path already names the real file and this is the identity.
std::wstring configFilePathForShell(const std::wstring& path);

#endif // TINTA_CONFIG_DIR_H

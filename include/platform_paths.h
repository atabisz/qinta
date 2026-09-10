#ifndef TINTA_PLATFORM_PATHS_H
#define TINTA_PLATFORM_PATHS_H

#include <string>

// The first seam of the platform layer: path and configuration-home lookups.
//
// Nothing in this header may include <windows.h>, "app.h", or any Direct2D or
// DirectWrite header, and that restriction is the entire point. settings.h
// includes app.h, which includes windows.h, d2d1.h and dwrite.h -- so any
// translation unit that includes settings.h can never compile on Linux, however
// platform-free its own logic happens to be. A file that only needs to find a
// directory should not thereby acquire a dependency on the Windows GUI stack.
//
// Paths are std::wstring to match the rest of the tree. That is deliberately
// NOT the string migration: on Windows these hold UTF-16, on POSIX UTF-32, and
// each implementation converts at the OS boundary. Moving the tree off wide
// strings is a separate job with its own cost, and this seam does not prejudge
// it either way.
namespace platform {

// Directory containing the running executable, with no trailing separator.
// Empty if it cannot be determined.
std::wstring executableDirectory();

// True only for an existing *regular file*. A directory returns false, which is
// what the portable-mode probe depends on: a folder named settings.ini must not
// be mistaken for the opt-in file.
bool regularFileExists(const std::wstring& path);

// Per-user configuration directory for `appName`, created if it does not exist:
//   Windows  %APPDATA%\<appName>
//   macOS    ~/Library/Application Support/<appName>
//   Linux    $XDG_CONFIG_HOME/<appName>, else ~/.config/<appName>
// Empty if it could not be resolved or created. Calling it twice is harmless.
std::wstring userConfigDirectory(const std::wstring& appName);

// L'\\' on Windows, L'/' everywhere else.
wchar_t pathSeparator();

} // namespace platform

#endif // TINTA_PLATFORM_PATHS_H

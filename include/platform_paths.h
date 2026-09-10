#ifndef TINTA_PLATFORM_PATHS_H
#define TINTA_PLATFORM_PATHS_H

#include <string>
#include <vector>

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

// UTF-8 <-> wide conversion at the OS boundary.
//
// These are here because the tree had reimplemented the same pair by hand in ten
// separate files -- toWide, toWideStr, refToWide, utf8ToWideTheme, toUtf8 -- each
// calling MultiByteToWideChar/WideCharToMultiByte directly, and each therefore
// requiring windows.h. The comment at the top of one of them says why the copies
// exist: "Local converters keep this file free of editor.cpp for the test
// target." A seam is the thing that was missing, so the copies collapse onto it.
//
// The duplicates were not identical, which is the other reason to have one:
// some passed an explicit length, others passed -1 and then trimmed the
// terminator. Those disagree on input containing an embedded NUL -- the -1 form
// truncates there. This pair preserves the full input in both directions, which
// matches the majority of the call sites it replaces.
//
// Empty in, empty out. Malformed UTF-8 yields U+FFFD rather than being dropped:
// a string we cannot decode should stay visibly wrong instead of silently
// becoming a different valid string.
std::wstring fromUtf8(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);

// The user's preferred UI languages as BCP 47 tags, most preferred first, e.g.
// { L"en-AU", L"ja-JP" }. Empty when the platform reports nothing usable --
// callers are expected to fall back to English, not to treat empty as an error.
//
// Tags are normalized to BCP 47 by each implementation so callers never have to
// know which shape the OS handed over. That matters on POSIX, where the native
// form is a locale name like "zh_CN.UTF-8": the underscore becomes a hyphen and
// the codeset and @modifier suffixes are dropped. The "C" and "POSIX" locales
// mean "no preference" and are omitted rather than reported as a language.
std::vector<std::wstring> preferredUiLanguages();

// Hand `path` to the desktop's default handler -- the equivalent of a
// double-click. Fire and forget: it does not wait for the application, and it
// reports nothing, because no caller in the tree checks and the failure mode
// that matters (no handler registered) is not distinguishable from success on
// every platform anyway.
void openInDefaultApplication(const std::wstring& path);

} // namespace platform

#endif // TINTA_PLATFORM_PATHS_H

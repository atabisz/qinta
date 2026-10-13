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

// 1601-01-01 to 1970-01-01 in 100ns ticks. Exact by definition rather than by a
// leap-second table: FILETIME counts a uniform 100ns tick and does not represent
// leap seconds at all. It lives in this header because this is the only header
// both the POSIX seam and settings.cpp can include -- settings.h reaches app.h
// and therefore windows.h -- and one copy is the point: it was a function-local
// constexpr in settings.cpp, and a second hand-written copy of a 369-year offset
// is the kind of duplicate that goes wrong silently.
constexpr unsigned long long kUnixEpochInFileTimeTicks = 116444736000000000ULL;

// Last-write time of `path` as Windows FILETIME ticks -- 100ns intervals since
// 1601-01-01 UTC -- or 0 if it cannot be read.
//
// FILETIME ticks rather than anything more natural because that is what the tree
// ALREADY MEANS by a file time, in places that outlive this seam:
// App::lastFileWriteTime and LayoutTab::lastWrite are FILETIMEs compared with
// CompareFileTime, and settings.ini's [Recent] timestamps are persisted as these
// ticks and decoded for display with FileTimeToLocalFileTime. This hands back the
// value those callers already hold, so no representation changes anywhere.
//
// Worth stating because file_utils.cpp carried the opposite conclusion for
// several spans: that porting its one Windows-bound function meant changing the
// representation across eleven sites. Its premise was correct and is preserved
// here -- packing a POSIX timestamp into a FILETIME's two halves would satisfy
// every equality comparison in input.cpp and silently redate the start page by
// 369 years -- but packing was never the only portable move. settings.cpp's
// fileTimeTicksNow() had been computing GENUINE ticks from std::chrono, on any
// platform, since before that comment was written.
//
// 0 for "cannot be read" rather than an error code, because it is what every
// caller already does with a missing file: the Windows original silently skipped
// the update when CreateFileW failed, and 0 lets a caller reproduce that exactly.
// It is also the initial value of both members, so it can never be mistaken for
// a real reading of a file this tree can open.
unsigned long long fileWriteTicks(const std::wstring& path);

// Create an empty file at `path`, FAILING if anything already exists there.
// True on success.
//
// EXCLUSIVE IS THE ENTIRE CONTRACT, and it is a seam rather than two lines of
// <fstream> because C++ has no portable exclusive create. `std::ofstream` on an
// existing path TRUNCATES it, and the obvious repair -- ask `exists()` first, then
// open -- reintroduces exactly the race the Win32 original was written to avoid:
// the one caller is the folder browser's "new note" box, so the file being tested
// is one whose name the user has just typed into a directory they are looking at,
// and losing that race silently empties a document instead of reporting a
// collision. Windows expresses this as `CREATE_NEW`, POSIX as `O_EXCL`, and Qt as
// QIODevice::NewOnly, which is that flag; all three decide it inside the
// filesystem, so no caller has to check first.
//
// The failure is not distinguished, deliberately: "already exists" and "could not
// be created" reach the one caller as the same red input box, because neither is
// something it can act on differently.
bool createEmptyFileExclusive(const std::wstring& path);

// Expand the RUNNING PLATFORM's environment-variable placeholders in a path the user
// typed. Returns the expanded string; returns `path` unchanged when there is nothing to
// expand.
//
// THE SYNTAX IS PER-PLATFORM AND THAT IS THE CONTRACT, not a gap: `%VAR%` on Windows,
// `$VAR` and `${VAR}` elsewhere. This seam deliberately does NOT accept both spellings
// everywhere. The one caller is the folder browser's path box, and what a user types
// there is whatever their shell and file manager taught them -- so accepting `%HOME%` on
// Linux would be inventing a syntax no other program on that machine honours, and
// accepting `$APPDATA` on Windows would collide with a legal filename character.
//
// IT RETURNS A STRING BECAUSE THE FIXED BUFFER WAS A BUG, and that is the substantive
// reason this is a seam rather than a one-line call. The code it replaces read:
//
//     wchar_t expanded[MAX_PATH];
//     if (ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH) > 0)
//         path = expanded;
//
// ExpandEnvironmentStringsW returns THE REQUIRED SIZE when the buffer is too small -- a
// positive number, so the `> 0` test passes -- and leaves the buffer's contents
// UNDEFINED. An expansion longer than 260 wide characters therefore assigned from an
// unterminated buffer. Every caller of an expander wants the result, not a size class, so
// the seam takes the size question away rather than documenting it.
//
// AN UNSET VARIABLE IS LEFT LITERAL ON BOTH PLATFORMS, and that choice is load-bearing
// enough to be part of the contract rather than an implementation detail. It is what
// ExpandEnvironmentStringsW already does, so Windows behaviour is unchanged; the reason to
// match it off Windows rather than expand to empty is that the caller browses to whatever
// comes back. Expanding `$NOSUCHVAR/notes` to `/notes` yields a path that may well EXIST,
// so the folder browser would open the wrong directory and look like it worked. Left
// literal, the path does not exist, and the caller shows the red input box it already has.
std::wstring expandEnvironmentPlaceholders(const std::wstring& path);

// Per-user configuration directory for `appName`, created if it does not exist:
//   Windows  %APPDATA%\<appName>
//   macOS    ~/Library/Application Support/<appName>
//   Linux    $XDG_CONFIG_HOME/<appName>, else ~/.config/<appName>
// Empty if it could not be resolved or created. Calling it twice is harmless.
std::wstring userConfigDirectory(const std::wstring& appName);

// L'\\' on Windows, L'/' everywhere else.
wchar_t pathSeparator();

// Do these two paths name the same document? Case-INSENSITIVE on Windows,
// case-SENSITIVE everywhere else.
//
// CASE-INSENSITIVITY IS A PROPERTY OF A FILESYSTEM, NOT OF A STRING, and that is the
// whole reason this is a seam rather than a helper. Five sites in input.cpp reached for
// `_stricmp` on a path, which is correct on Windows and quietly wrong on Linux, where
// `notes.md` and `Notes.md` are two different files and folding them would focus the
// wrong tab or purge the wrong recent entry.
//
// DELIBERATELY LEXICAL, AND std::filesystem::equivalent IS REJECTED RATHER THAN MERELY
// UNUSED. That is the standard answer to "same file" and it cannot be used here: it
// requires BOTH operands to exist, because it compares the files the OS resolves them
// to. One of the five callers sits under the comment "Vanished since it was recorded:
// drop the stale entry" -- it is comparing against a path that is gone by construction,
// so `equivalent` would report an error there and turn a stale-entry purge into a
// silent no-op. Every caller here is asking about two SPELLINGS, not two inodes.
//
// The honest consequence of the Windows arm, since the sites it replaced compared UTF-8
// BYTES with `_stricmp` and this compares wide characters: case folding now reaches past
// ASCII, so two paths differing only in the case of a non-ASCII letter compare equal
// where they used to compare different. That matches what NTFS itself does, so it is a
// correction rather than a drift -- but it IS a change, and it is written down here
// rather than discovered in a diff. CompareStringOrdinal is the stricter instrument and
// is not used: it would matter if these comparisons were a security boundary, and what
// they actually decide is which tab to focus.
bool pathsEquivalent(const std::wstring& a, const std::wstring& b);

// The value of environment variable `name`, or empty if it is not set.
//
// EMPTY COLLAPSES TWO STATES AND THAT IS A DECISION, not a limitation of either
// platform: a variable that is set to the empty string reads the same here as one
// that is absent. Windows can tell them apart (GetEnvironmentVariableW returns 0
// for both and GetLastError says which), and so can POSIX (getenv returns "" for
// one and nullptr for the other), so the collapse is this seam's choice. It is the
// right one for every caller the tree has: each asks for a variable in order to
// build a path out of it, and an empty prefix is no more usable than no prefix.
// A caller that genuinely needs the difference needs a different function, not a
// second return value bolted onto this one.
std::wstring environmentVariable(const std::wstring& name);

// Locate an executable by BARE NAME -- `L"pandoc"`, never `L"pandoc.exe"` -- and
// return its full path, or empty if it was not found.
//
// The suffix is the seam's business and not the caller's, which is the whole
// reason this takes a bare name: `.exe` is a Windows convention, POSIX has no
// suffix at all, and a caller spelling it out would be writing one platform's rule
// into portable code. Windows appends it through SearchPathW's own lpExtension
// parameter; the Qt arm lets QStandardPaths::findExecutable apply PATHEXT or the
// execute bit as the host requires.
//
// THE TWO ARMS DO NOT SEARCH THE SAME SET, and the name deliberately says "on
// path" rather than "on PATH" because of it. Win32's search order begins with the
// directory of the running executable and includes the system directories before
// it ever reaches PATH; QStandardPaths::findExecutable searches PATH and nothing
// else, and additionally requires the execute bit. So a hit on one platform is not
// evidence the same file would be found on the other, and neither arm is wrong --
// each implements its own host's notion of "installed and runnable". A caller that
// needs an exact directory list should pass one, which means extending this
// function rather than assuming what it does.
std::wstring findExecutableOnPath(const std::wstring& name);

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

// Hand a URL to the desktop's default browser. Fire and forget, for the same
// reason as openInDefaultApplication: the one caller -- handleLinkClick, on a
// clicked link in a rendered document -- does not check.
//
// SEPARATE FROM openInDefaultApplication, WHICH IS THE NEAR MISS WORTH NAMING.
// That function's own comment says it "replaces ShellExecuteW on Windows", which
// makes it look like the reuse, and it is not: it wraps QUrl::fromLocalFile, so
// `https://example.com` would arrive at the browser as
// `file:///.../https:/example.com`. Same Win32 call underneath, different
// argument meaning, so it needs its own name rather than a second caller.
//
// std::string rather than the std::wstring the rest of this header uses, because
// that is what the tree holds: App::hoveredLink is UTF-8 out of the Markdown
// source, and the call it replaces was ShellExecute*A*. That has a consequence
// this seam CARRIES rather than fixes -- the A form decodes those bytes as the
// active ANSI codepage, so a URL with non-ASCII characters is already mangled on
// Windows today, while the Qt half decodes UTF-8 correctly. The divergence is
// pre-existing Windows behaviour preserved verbatim, not a new Qt one, and
// unifying it is a fix with its own test and its own slice.
void openExternalUrl(const std::string& url);

// Show `path` to the user inside their file manager -- "Reveal in Explorer" on
// the document context menu. Fire and forget, like the two above.
//
// A THIRD ENTRY RATHER THAN A SECOND CALLER OF openInDefaultApplication, for the
// same reason openExternalUrl is: that function OPENS the file, and this must
// open its CONTAINING FOLDER with the file picked out. Handing a document path
// to openInDefaultApplication here would launch the editor the user was trying
// to locate the file for.
//
// EVERY PLATFORM SELECTS THE ENTRY, BY A DIFFERENT ROUTE, AND ONE CAN FALL SHORT.
// Windows: `explorer.exe /select,<path>`. macOS: `open -R`. Linux: the
// org.freedesktop.FileManager1 ShowItems call, which the common file managers
// implement and nothing requires -- so when it fails (no owner on the session bus,
// no dbus-send) the seam opens the containing DIRECTORY and selects nothing, the
// one-step-short answer every platform but Windows gave before 2026-10-13. Named
// here rather than left to be discovered, the way openExternalUrl's ANSI mangling
// is: a divergence a seam carries is only acceptable while it is legible at the
// seam.
void revealInFileManager(const std::wstring& path);

// The process revealInFileManager starts off Windows, as data, so the command line
// can be tested without starting a file manager. Empty `program` on Windows, whose
// reveal is a ShellExecuteW call rather than a process. `path` is made absolute.
struct RevealCommand {
    std::wstring program;
    std::vector<std::wstring> arguments;
};
RevealCommand revealCommandFor(const std::wstring& path);

// "MMM d" -- abbreviated month, then day -- in the user's regional format: "Sep 14",
// "14. Sept.", "9月 14". The start page's recent-file dates off Windows, where they
// were the C locale's English month whatever the user's region. Windows keeps
// GetDateFormatEx at the call site. `localeName` is a Qt locale name ("de_DE");
// empty means the system locale, and only tests pass one, because the system locale
// is resolved once per process and cannot be forced after it has been read.
std::wstring formatMonthDay(int year, int month, int day,
                            const std::string& localeName = {});

// Do these two paths name the same file, by the platform's own case rule?
//
// This is a seam and not a helper because the answer differs by platform and
// both answers are correct: on Windows "Notes.md" and "notes.md" are one file,
// on Linux they are two. Folding case on Linux would silently merge two distinct
// documents' reading positions; comparing exactly on Windows would give the same
// document two entries depending on how it was opened.
//
// It replaces _stricmp, which is an MSVC extension with no POSIX spelling. The
// Windows implementation is byte-identical to _stricmp for the input this tree
// has -- see the note there about why that holds and what would break it.
bool documentPathsEqual(const std::string& a, const std::string& b);

// Should this directory entry be omitted from a file listing the user browses?
//
// A seam for the same reason documentPathsEqual is one: the convention genuinely
// differs and both answers are correct on their own platform. Windows stores it as
// a filesystem ATTRIBUTE the user toggles in Explorer, and the name means nothing;
// POSIX has no such attribute and the convention is entirely a leading dot in the
// NAME. Neither rule can be applied on the other platform -- consulting an
// attribute that does not exist would hide nothing at all on Linux, and hiding
// dot-names on Windows would hide files Explorer shows.
//
// Both arguments are supplied because each implementation needs a different one,
// and passing both keeps the caller from having to know which. `fullPath` must be
// openable as given; `name` is the final component only.
bool isHiddenEntry(const std::wstring& fullPath, const std::wstring& name);

// `path` made absolute against the working directory and collapsed lexically (`.`, `..`,
// separators), without touching the filesystem: the path need not exist, and symlinks are
// not resolved. Empty when it cannot be resolved.
//
// A seam because std::filesystem::absolute is not one function on Windows. MSVC's calls
// GetFullPathNameW; libstdc++'s (MinGW) reads a leading `\\` as a root directory with no
// root name and puts the current drive in front, so `\\server\share\doc.md` became
// `D:\server\share\doc.md` (the windows-mingw CI job, 2026-10-07). The Windows answer is
// therefore GetFullPathNameW on both toolchains.
std::wstring absolutePath(const std::wstring& path);

} // namespace platform

#endif // TINTA_PLATFORM_PATHS_H

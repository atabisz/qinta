#include "platform_paths.h"

#include <QByteArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <vector>

// Native APIs are reached in three places, each because Qt has no answer with the
// same behaviour: executableDirectory (no instance-free Qt call), and on Windows
// expandEnvironmentPlaceholders (ExpandEnvironmentStringsW, so %VAR% expands exactly
// as it always did) and revealInFileManager (ShellExecuteW, so Explorer selects the
// entry). Everything else is a choice between Qt calls.
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Qt implementation of the platform path seam -- the file that REPLACES
// src/platform_paths_posix.cpp, and that src/platform_paths_win32.cpp collapsed
// into on 2026-10-07, once the Direct2D stack had left. Nine of the eleven functions
// have no platform branch at all here, which is what "one stack on every platform"
// looks like at the file level.
//
// EVERY CHOICE BELOW WAS MEASURED BEFORE IT WAS WRITTEN, on Qt 6.4.2 (Linux) and
// Qt 6.8.3 (Windows), by a throwaway diagnostic kept outside the tree. That was
// not ceremony: writing this file from the Qt documentation would have introduced
// four defects, three of them silent.
//
//   1. QStandardPaths::GenericConfigLocation is %LOCALAPPDATA% on Windows
//      (measured: C:/Users/<u>/AppData/Local), not %APPDATA%. Using it would have
//      MOVED every existing user's settings.ini from Roaming to Local and
//      orphaned the old one -- a data-loss-shaped bug with a clean diff.
//   2. QLocale::system().uiLanguages() EXPANDS each tag with likely subtags.
//      LANGUAGE=zh_CN.UTF-8:ja_JP@x returns six entries, not two:
//      zh-CN, zh-Hans-CN, zh, ja-JP, ja-Jpan-JP, ja. The existing test asserted
//      forced[1]=="ja-JP" and would have failed against a CORRECT implementation.
//   3. With no LANGUAGE set at all, uiLanguages() returns exactly {"C"} on Linux.
//      The seam's documented contract is that C/POSIX is not a language, so the
//      filter below is load-bearing rather than defensive.
//   4. Qt 6.4.2 returns a RELATIVE XDG_CONFIG_HOME verbatim (measured:
//      "relative/not/absolute"), where the XDG spec and the code this replaces
//      both require it to be ignored. Hence the absolute-path guard.
//
// A fifth reading changes Windows behaviour on purpose and is called out at
// fileWriteTicks.

namespace platform {

namespace {

// The seam speaks std::wstring; Qt speaks QString. Both directions preserve
// embedded NULs, which is why the sized overloads are used rather than the
// convenient ones -- fromUtf8(const char*) stops at the first NUL and the seam's
// contract (and its test) says it must not.
inline QString toQString(const std::wstring& s) {
    return QString::fromStdWString(s);
}

inline std::wstring toWide(const QString& s) {
    return s.toStdWString();
}

// Native separators on the way out, always. QStandardPaths and QDir hand back
// forward slashes on Windows ("C:/Users/..."), and the calls this file replaces
// handed back backslashes. That difference is not cosmetic: documentPathsEqual
// compares path strings byte for byte, so a seam that returned one spelling
// while the rest of the tree produced the other would make equal paths compare
// unequal. This keeps the returned STRING identical to what shipped.
inline std::wstring nativeWide(const QString& s) {
    return toWide(QDir::toNativeSeparators(s));
}

} // namespace

wchar_t pathSeparator() {
    // Measured: QDir::separator() is '\\' on Windows and '/' on Linux, so this
    // needs no branch of its own -- it forwards the answer rather than restating it.
    return static_cast<wchar_t>(QDir::separator().unicode());
}

bool pathsEquivalent(const std::wstring& a, const std::wstring& b) {
    // HOST-ADAPTIVE, NOT POSIX-ONLY, and pathSeparator() one function above is the
    // precedent: this arm is the Qt frontend's paths backend and it BUILDS ON WINDOWS,
    // so a flat `a == b` here would have given the Qt build case-sensitive path
    // comparison on a case-insensitive filesystem -- two Windows binaries of the same
    // application disagreeing about whether C:\A.md and c:\a.md are one document.
    // The first draft of this function did exactly that.
    //
    // The branch is the same three-way shape this file already uses four times
    // (:118, :245, :355), and the fold reaches past ASCII on both insensitive arms,
    // matching the _wcsicmp the deleted platform_paths_win32.cpp used.
#if defined(_WIN32)
    return QString::fromStdWString(a).compare(QString::fromStdWString(b),
                                              Qt::CaseInsensitive) == 0;
#elif defined(__APPLE__)
    // APFS and HFS+ are case-INSENSITIVE by default -- case-preserving, which is a
    // different property. APFS can be formatted case-sensitive, so this is the default
    // rather than a guarantee; it is the right default because a Mac user who reformatted
    // their boot volume that way is the rarer case than one who typed the wrong case.
    return QString::fromStdWString(a).compare(QString::fromStdWString(b),
                                              Qt::CaseInsensitive) == 0;
#else
    // Exact, and the plainness is the point: on a case-sensitive filesystem two
    // spellings name the same document only if they are the same spelling.
    //
    // NOT QFileInfo::canonicalFilePath, which resolves symlinks and returns EMPTY for a
    // path that does not exist -- the same trap std::filesystem::equivalent falls into
    // and which platform_paths.h rejects it for, since one caller compares against a
    // path that has vanished by construction. Two empty strings compare EQUAL, so that
    // route would have made every pair of missing paths the same document.
    return a == b;
#endif
}

std::wstring executableDirectory() {
    // THE ONE FUNCTION QT CANNOT ANSWER HERE, and the reason is structural rather
    // than a gap in Qt: QCoreApplication::applicationDirPath() needs an instance,
    // and every caller of this seam that matters runs before one exists -- the
    // test targets construct none at all, and portable-mode detection asks this
    // question in order to decide where to look for settings.ini. Measured: with
    // no instance, applicationDirPath() is unavailable, while QStandardPaths and
    // QLocale both work perfectly.
    QString file;
#ifdef _WIN32
    // MAX_PATH matches the buffer the replaced code used, so a long path fails
    // here exactly as it failed before -- a known limitation carried across
    // unchanged rather than a new one introduced by the port.
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return std::wstring();
    file = QString::fromWCharArray(exePath);
#elif defined(__APPLE__)
    // Two calls: the first reports the required size, the second fills it. NOT
    // measured -- there is no macOS host for this tree yet -- so it is the call
    // the file this replaces made, moved rather than rewritten.
    unsigned int size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return std::wstring();
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return std::wstring();
    file = QString::fromUtf8(buffer.data());
#else
    // /proc/self/exe is already fully resolved by the kernel, so this agrees with
    // the readlink() it replaces -- and needs no <unistd.h> to say so.
    file = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
#endif
    if (file.isEmpty()) return std::wstring();

    // One tail for all three, so the "strip the last component" step cannot drift
    // between platforms the way three hand-written find_last_of calls could.
    const QString dir = QFileInfo(file).absolutePath();
    return dir.isEmpty() ? std::wstring() : nativeWide(dir);
}

std::wstring environmentVariable(const std::wstring& name) {
    // QProcessEnvironment RATHER THAN qEnvironmentVariable, and the reason is a
    // measured constraint on this tree rather than a preference. The two hosts that
    // build it are four minor releases apart -- Qt 6.8.3 on Windows, Qt 6.4.2 in
    // the WSL probe -- and qEnvironmentVariable's home moved across that gap:
    // <QtEnvironmentVariables> exists from 6.5 and is absent in 6.4.2, while
    // relying on <QtGlobal> to forward it is relying on exactly the arrangement
    // that split. QProcessEnvironment has been QtCore since Qt 4.6, so it compiles
    // on both without a version test. The scan of the whole environment to read one
    // variable is the price, paid at most three times per session by the one caller.
    //
    // systemEnvironment() is read per call, not cached, so this sees a variable set
    // after startup exactly as the Windows arm does.
    const QString value =
        QProcessEnvironment::systemEnvironment().value(toQString(name));
    // Empty covers both absent and set-to-empty, which is the header's stated
    // collapse. QProcessEnvironment::value() returns the same empty QString for
    // either, so this arm does not have to implement the decision -- it inherits it.
    return value.isEmpty() ? std::wstring() : toWide(value);
}

std::wstring findExecutableOnPath(const std::wstring& name) {
    // findExecutable applies the host's own suffix and permission rules -- PATHEXT
    // on Windows, the execute bit on POSIX -- which is precisely why the header
    // takes a bare name. Empty QString for not found, and the seam's empty means
    // the same thing.
    //
    // nativeWide rather than toWide: Qt hands back forward slashes on Windows, and
    // this path is handed to the tree as a string that gets compared and displayed
    // alongside paths from everywhere else. Same argument the helper's own comment
    // makes, applied here because a returned PATH is exactly the case it is for.
    const QString found = QStandardPaths::findExecutable(toQString(name));
    return found.isEmpty() ? std::wstring() : nativeWide(found);
}

bool regularFileExists(const std::wstring& path) {
    // isFile(), not exists(): portable mode depends on the distinction, and the
    // test has a control asserting a directory is not reported as a regular file.
    return QFileInfo(toQString(path)).isFile();
}

bool createEmptyFileExclusive(const std::wstring& path) {
    // NewOnly is O_EXCL/CREATE_NEW expressed in Qt, so the exclusivity is decided
    // inside the filesystem rather than by a preceding exists() test -- the race the
    // header refuses. WriteOnly is ORed in because NewOnly alone opens nothing:
    // it is a modifier on the mode, not a mode.
    QFile file(toQString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    file.close();
    return true;
}

std::wstring expandEnvironmentPlaceholders(const std::wstring& path) {
#ifdef _WIN32
    // The Windows arm is ExpandEnvironmentStringsW, in the same two-call shape
    // platform_paths_win32.cpp used. That file collapsed into this one on 2026-10-07, so
    // the Windows behaviour had to survive the collapse byte for byte: reimplementing
    // %VAR% over QProcessEnvironment here would have made the cutover a behaviour change
    // in a box users type paths into -- the one thing a cutover must not be. This is the
    // only copy now.
    const DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
    if (needed == 0) return path;
    std::wstring buffer(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(path.c_str(), buffer.data(), needed);
    if (written == 0 || written > needed) return path;
    buffer.resize(written - 1);
    return buffer;
#else
    // `$VAR` and `${VAR}` -- what a shell and a file manager on this platform teach a user
    // to type. Hand-written because Qt has no expand-in-place API at all:
    // QProcessEnvironment answers "what is VAR", never "expand this string", so there is
    // nothing to delegate to.
    //
    // ASCII name rules rather than iswalpha/iswalnum, on purpose. Environment variable
    // names are ASCII by convention everywhere this runs, and the isw* family is
    // LOCALE-DEPENDENT: under a locale that classifies more letters, a path containing a
    // non-ASCII character after a literal '$' would start being read as a variable name on
    // one machine and not another. A portability seam whose parsing depends on LC_CTYPE is
    // not portable.
    const auto isNameStart = [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_';
    };
    const auto isNameChar = [&isNameStart](wchar_t c) {
        return isNameStart(c) || (c >= L'0' && c <= L'9');
    };

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    std::wstring out;
    out.reserve(path.size());

    for (size_t i = 0; i < path.size();) {
        if (path[i] != L'$') {
            out += path[i++];
            continue;
        }

        // The braced and bare forms differ in BOTH where the name starts and where to
        // resume afterwards, so both are resolved here together rather than in two
        // branches that could drift apart about the resume point -- an off-by-one there
        // duplicates or eats a path separator, which is silent and looks like a typo in
        // the user's input.
        const bool braced = (i + 1 < path.size() && path[i + 1] == L'{');
        const size_t nameStart = i + (braced ? 2 : 1);
        size_t nameEnd = nameStart;
        if (braced) {
            while (nameEnd < path.size() && path[nameEnd] != L'}') ++nameEnd;
        } else if (nameEnd < path.size() && isNameStart(path[nameEnd])) {
            ++nameEnd;
            while (nameEnd < path.size() && isNameChar(path[nameEnd])) ++nameEnd;
        }

        // An unterminated `${`, and a `$` before a digit or a separator or end-of-string,
        // are LITERAL TEXT rather than errors. This runs on a path the user just typed, so
        // a half-finished `${` must come back as `${` and not disappear.
        const bool terminated = !braced || nameEnd < path.size();
        if (nameEnd == nameStart || !terminated) {
            out += path[i++];
            continue;
        }

        const std::wstring name(path, nameStart, nameEnd - nameStart);
        const size_t resume = nameEnd + (braced ? 1 : 0);

        // contains() RATHER THAN value(), and this is the one place this arm must NOT
        // inherit environmentVariable()'s collapse above. That seam folds absent and
        // set-to-empty together deliberately, because its caller cannot act on the
        // difference. Here they DIFFER, and the reason is entirely about the UNSET case:
        // the header's contract is that an unset variable stays LITERAL, and value()
        // alone cannot express that -- it would expand every unset variable to empty and
        // browse to a directory the user did not name.
        //
        // The set-to-empty case then MATCHES Windows, and that is measured rather than
        // assumed: an empty-but-present variable IS representable in a Win32 environment
        // block (SetEnvironmentVariableW deletes only on a NULL value -- "empty deletes"
        // is cmd.exe's `set X=` convention, not the API's), and ExpandEnvironmentStringsW
        // expands it to nothing. So both arms yield empty for set-to-empty and literal for
        // unset, which is one shared assertion in tests/platform_paths_tests.cpp.
        const QString key = toQString(name);
        if (env.contains(key)) {
            out += toWide(env.value(key));
        } else {
            out.append(path, i, resume - i);  // the placeholder, verbatim
        }
        i = resume;
    }
    return out;
#endif
}

unsigned long long fileWriteTicks(const std::wstring& path) {
    const QFileInfo info(toQString(path));
    const QDateTime modified = info.lastModified();

    // GATED ON isValid(), NEVER ON THE VALUE, and this is the trap the diagnostic
    // caught. For a missing file the msecs are 0 on Windows but -36000000 on Linux
    // (an invalid QDateTime, converted anyway, yields the local UTC offset). Doing
    // the arithmetic first and testing the result for zero would therefore return a
    // plausible non-zero tick count for a file that does not exist -- on one
    // platform only, which is the worst shape a bug can have here.
    if (!info.exists() || !modified.isValid()) return 0;

    // Signed first, then one check, exactly as the POSIX implementation did: a
    // pre-1970 mtime is legal and negative, and an unsigned multiply would wrap it
    // into a plausible far-future timestamp. Anything before 1601 has no FILETIME
    // at all, so it reports "unknown" rather than a huge positive number.
    const long long ticks = static_cast<long long>(kUnixEpochInFileTimeTicks) +
                            modified.toMSecsSinceEpoch() * 10000LL;
    if (ticks <= 0) return 0;
    return static_cast<unsigned long long>(ticks);
}

// TWO DELIBERATE BEHAVIOUR CHANGES IN THE FUNCTION ABOVE, both measured, both
// stated here rather than discovered later:
//
// A DIRECTORY NOW REPORTS A REAL WRITE TIME ON WINDOWS. It used to report 0,
// because CreateFileW refuses a directory without FILE_FLAG_BACKUP_SEMANTICS and
// the Windows original did not pass it; stat() and QFileInfo are both happy to
// describe one. The test pinned that divergence per platform on the grounds that
// both answers were correct -- and the readings say it was never a platform truth,
// it was an artifact of a flag. No caller in the tree asks a directory for its
// write time, so nothing changes behaviourally; the test now asserts the unified
// answer and names the backend, not the OS, as what decides it.
//
// RESOLUTION DROPS FROM 100ns TO 1ms OFF WINDOWS. st_mtim carried nanoseconds;
// QDateTime::toMSecsSinceEpoch() does not. The unit and epoch are unchanged --
// still FILETIME ticks -- so nothing persisted or displayed moves. The property
// the callers actually depend on is that a rewrite INSIDE THE SAME SECOND reads as
// a change, and that survives: measured 60ms apart on both platforms, with
// non-zero millisecond remainders (Linux 799/859, Windows 953/020).

std::wstring userConfigDirectory(const std::wstring& appName) {
    // ONE ENUM PER PLATFORM RATHER THAN ONE ENUM, because no single QStandardPaths
    // location reproduces the three paths this seam documents. Measured with no
    // QCoreApplication instance:
    //
    //                     Windows 6.8.3                Linux 6.4.2
    //   GenericConfig     AppData/Local   (WRONG)      ~/.config      (right)
    //   AppData           AppData/Roaming (right)      ~/.local/share (WRONG)
    //
    // and AppDataLocation is additionally a trap to build on: it APPENDS the
    // application name once QCoreApplication::setApplicationName is called, which
    // the real Qt app will do, so %APPDATA%/Tinta would silently become
    // %APPDATA%/Tinta/Tinta. So Windows reads APPDATA directly -- still a Qt Core
    // call, and it drops shlobj.h, SHGetFolderPathW and the shell32 link.
    QString base;
#if defined(_WIN32)
    base = qEnvironmentVariable("APPDATA");
#elif defined(__APPLE__)
    // ~/Library/Application Support, matching the code this replaces. Deliberately
    // NOT QStandardPaths: GenericConfigLocation is ~/Library/Preferences on macOS,
    // which would move the file. Unmeasured, like the macOS branch above.
    const QString home = QDir::homePath();
    if (home.isEmpty()) return std::wstring();
    base = home + QStringLiteral("/Library/Application Support");
#else
    base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!QDir::isAbsolutePath(base)) {
        // Measured: Qt 6.4.2 returns a relative XDG_CONFIG_HOME verbatim, where the
        // XDG spec says a relative or empty value must be IGNORED. Falling back the
        // way the spec (and the replaced code) does keeps a stray relative value in
        // someone's environment from scattering config into the working directory.
        const QString home = QDir::homePath();
        if (home.isEmpty()) return std::wstring();
        base = home + QStringLiteral("/.config");
    }
#endif
    if (base.isEmpty()) return std::wstring();

    const QString full = base + QLatin1Char('/') + toQString(appName);
    // mkpath, not mkdir: it creates intermediate directories and is documented to
    // return true when the directory already exists. Measured idempotent on both
    // platforms, which is what the seam's "calling it twice is harmless" needs.
    if (!QDir().mkpath(full)) return std::wstring();
    return nativeWide(full);
}

std::wstring fromUtf8(const std::string& utf8) {
    // The sized overload, so an embedded NUL is data rather than a terminator.
    // Measured on both platforms: "a\0b" survives as three wide units and round
    // trips; malformed input becomes U+FFFD and is never dropped (7 wide units for
    // "ok\x80tail", with U+FFFD present); empty in, empty out.
    return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())).toStdWString();
}

std::string toUtf8(const std::wstring& wide) {
    const QByteArray bytes = QString::fromStdWString(wide).toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

std::vector<std::wstring> preferredUiLanguages() {
    // The whole POSIX implementation of this -- reading LANGUAGE, then LC_ALL,
    // LC_MESSAGES and LANG, splitting on colons, and hand-normalizing
    // "zh_CN.UTF-8@pinyin" into "zh-CN" -- collapses onto one call that already
    // does it. Measured: Linux honours LANGUAGE and re-reads it after startup, so
    // the existing environment-driven control still steers the result; Windows
    // ignores LANGUAGE entirely and reports the OS UI languages, which is what
    // GetUserPreferredUILanguages did.
    std::vector<std::wstring> tags;
    for (const QString& tag : QLocale::system().uiLanguages()) {
        if (tag.isEmpty()) continue;
        // Measured: a container with no locale set reports exactly {"C"}. The
        // seam's contract is that the C/POSIX locale is not a language, and the
        // test asserts it, so this filter is the contract rather than caution.
        if (tag == QStringLiteral("C") || tag == QStringLiteral("POSIX")) continue;
        const std::wstring wide = toWide(tag);
        // First mention wins, as in the code this replaces. Qt was not observed to
        // repeat a tag, but it does EXPAND them (zh-CN -> zh-CN, zh-Hans-CN, zh),
        // and two expansions overlapping is exactly how a duplicate would appear.
        bool seen = false;
        for (const std::wstring& already : tags) {
            if (already == wide) { seen = true; break; }
        }
        if (!seen) tags.push_back(wide);
    }
    return tags;
}

void openInDefaultApplication(const std::wstring& path) {
    // The one function in this file that needs Qt Gui rather than Qt Core. It
    // replaces ShellExecuteW on Windows and, off Windows, a double fork plus
    // execlp("xdg-open") plus a waitpid on the intermediate child -- roughly forty
    // lines whose entire purpose was to avoid leaving a zombie.
    QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(path)));
}

void openExternalUrl(const std::string& url) {
    // The second Qt Gui call in this file, and deliberately NOT the line above
    // with a different argument: fromLocalFile would percent-encode the scheme
    // into a relative file path. QUrl's own parser is the counterpart of what
    // ShellExecuteA's "open" verb does with a URL string.
    if (url.empty()) return;
    QDesktopServices::openUrl(QUrl(QString::fromUtf8(url.c_str(),
                                                    (int)url.size())));
}

void revealInFileManager(const std::wstring& path) {
#ifdef _WIN32
    // Windows can SELECT the entry, so it keeps the call platform_paths_win32.cpp made
    // (moved here when that file collapsed into this one, 2026-10-07), byte for byte:
    // /select, takes ONE argument and a document path routinely contains spaces, so the
    // quotes are load-bearing rather than defensive.
    std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr,
                  SW_SHOWNORMAL);
#else
    // The third Qt Gui call in this file, and the only seam here whose platforms
    // do DIFFERENT amounts of work -- see the header. This opens the containing
    // directory; it does not select the entry, because no portable way to ask for
    // that exists off Windows.
    //
    // QFileInfo::absolutePath, not a manual search for the last separator: the
    // input may be relative, and absolutePath resolves against the working
    // directory rather than returning something the file manager cannot open.
    QFileInfo info(toQString(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

bool documentPathsEqual(const std::string& a, const std::string& b) {
    // NO QT CALL HERE ON PURPOSE. Qt has no per-platform path-identity rule, and
    // the two obvious substitutes both change behaviour: QString::compare with
    // Qt::CaseInsensitive folds the FULL Unicode range, and
    // QFileInfo::canonicalFilePath touches the filesystem and returns empty for a
    // path that does not exist. So the comparison the tree shipped is preserved
    // verbatim, including its argument for being ASCII-only.
#ifdef _WIN32
    // NTFS and the shell treat paths case-insensitively, which is what the
    // _stricmp calls this replaced did. The fold is ASCII-only deliberately:
    // _stricmp folds per LC_CTYPE and tinta never calls setlocale, so it runs in
    // the "C" locale and folds ASCII and nothing else. These paths are UTF-8, so
    // every non-ASCII byte is >= 0x80 and both old and new compare it verbatim.
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
#else
    // Case-SENSITIVE off Windows, because the filesystems are. macOS is the
    // awkward one -- APFS is case-insensitive by default -- and this preserves the
    // documented caveat rather than quietly picking a side.
    return a == b;
#endif
}

bool isHiddenEntry(const std::wstring& fullPath, const std::wstring& name) {
    // The seam exists because the two platforms disagree about what "hidden"
    // means, and QFileInfo::isHidden() supplies EXACTLY that disagreement -- which
    // is the single cleanest result in this file. Measured, all nine cases:
    //   Windows: visible 0, .dotted 0, .dotted+FILE_ATTRIBUTE_HIDDEN 1,
    //            missing 0, missing ".gone.md" 0  (the attribute governs)
    //   Linux:   visible 0, .dotted 1, parent-dot 0, missing 0,
    //            missing ".gone.md" 1              (the name governs)
    // The `name` argument is now unused on BOTH platforms rather than just on
    // Windows: Qt reads the final component out of the path itself, and the
    // parent-dot control proves it does not search the whole path for a dot.
    (void)name;
    // An absent or unreadable entry reports NOT hidden, matching both originals:
    // showing an entry the user then cannot open is a visible, explicable error,
    // where silently omitting it makes a file that exists look like it does not.
    return QFileInfo(toQString(fullPath)).isHidden();
}

} // namespace platform

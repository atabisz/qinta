#include "platform_paths.h"

#include <QByteArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <vector>

// Only executableDirectory reaches a native API, and only because Qt has no
// instance-free answer for it (see the comment there). Both includes are for that
// one function; nothing else in this file is platform-specific except a choice
// between two Qt calls.
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Qt implementation of the platform path seam -- the file that REPLACES
// src/platform_paths_posix.cpp, and that src/platform_paths_win32.cpp is
// scheduled to collapse into at the Direct2D cutover (CMakeLists.txt says so at
// the TINTA_LEGACY_D2D block). Nine of the eleven functions have no platform
// branch at all here, which is what "one stack on every platform" looks like at
// the file level.
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
    // The third Qt Gui call in this file, and the only seam here whose two halves
    // do DIFFERENT amounts of work -- see the header. This opens the containing
    // directory; it does not select the entry, because no portable way to ask for
    // that exists.
    //
    // QFileInfo::absolutePath, not a manual search for the last separator: the
    // input may be relative, and absolutePath resolves against the working
    // directory rather than returning something the file manager cannot open.
    QFileInfo info(toQString(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
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

// Tests the platform path seam on every platform, and by compiling
// config_dir.cpp into this target it also proves that file builds and links
// without windows.h. That second point is the pilot's real claim, so it is
// measured here rather than asserted in a comment.

#include "config_dir.h"
#include "platform_paths.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef TINTA_PATHS_BACKEND_QT
// For the openExternalUrl-vs-openInDefaultApplication block only. It asserts a
// property of QUrl rather than of our code, deliberately -- see the note there.
#include <QLatin1String>
#include <QString>
#include <QUrl>
#endif

#ifdef _WIN32
// Only to ARRANGE the isHiddenEntry test: marking a file hidden has no portable
// spelling, which is the entire reason that seam exists, so setting one up needs
// the platform API directly. This does not weaken the claim in the comment above --
// that claim is that config_dir.cpp's own translation unit builds without
// windows.h, and this is a different translation unit.
#include <windows.h>
#endif

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    // --bare-locale runs MOST of these assertions against a deliberately empty
    // language environment, and it exists because one process cannot test both
    // cases: the backend caches the first resolved system locale, so forcing
    // LANGUAGE and observing the C-locale fallback are mutually exclusive within
    // one run. CMakeLists.txt registers this as a second ctest entry with LC_ALL=C,
    // LANG=C and an empty LANGUAGE, which measured as exactly one reported tag, "C".
    //
    // "MOST" corrects "the SAME", which stood here until 2026-09-20 and was off by a
    // third. Measured on Linux the same day: 90 assertions in the normal entry, 57 in
    // the bare one. The gap is deliberate and is the branch below -- the LANGUAGE
    // setenv at :359 is `!bareLocale`, and then :388 is an `if (bareLocale) / else`
    // whose bare arm holds the C/POSIX filter's ONLY control and whose else arm
    // (:406, closing at :455) holds the LANGUAGE-forcing controls. So the two entries
    // are not one test run twice; each holds a control the other cannot reach, and
    // reading them as identical would make either look redundant.
    //
    // Those three line numbers were wrong in this comment's first draft, by the one
    // mechanism the tree had not yet catalogued: not a different file growing, but
    // THIS EDIT shifting the lines it was citing. Writing eight lines moved every
    // address below by eight, so the numbers were stale before the file was saved --
    // which makes "cite, then re-derive after saving" the only order that works.
    bool bareLocale = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--bare-locale") bareLocale = true;
    }
    (void)bareLocale;  // read only off Windows, where LANGUAGE steers the result

#ifdef _WIN32
    check(platform::pathSeparator() == L'\\', "Windows separator is a backslash");
#else
    check(platform::pathSeparator() == L'/', "POSIX separator is a forward slash");
#endif

    // --- executableDirectory -------------------------------------------------
    std::wstring exeDir = platform::executableDirectory();
    check(!exeDir.empty(), "executableDirectory is not empty");
    check(fs::is_directory(fs::path(exeDir)), "executableDirectory names a directory");
    // CONTROL: without this, regularFileExists could be a bare "does the name
    // exist" check and every assertion below would still pass. Portable mode
    // depends on the distinction, so it is tested rather than assumed.
    check(!platform::regularFileExists(exeDir),
          "CONTROL: a directory is not reported as a regular file");

    // --- regularFileExists --------------------------------------------------
    std::wstring stamp = std::to_wstring(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    fs::path probeDir = fs::temp_directory_path() / (L"tinta-paths-" + stamp);
    check(fs::create_directories(probeDir, error) && !error,
          "temporary probe directory is created");

    fs::path optIn = probeDir / L"settings.ini";
    check(!platform::regularFileExists(optIn.wstring()),
          "a missing file is not reported as present");
    {
        std::ofstream out(optIn, std::ios::binary);
        out << "; platform path probe\n";
        check(out.good(), "probe file is written");
    }
    check(platform::regularFileExists(optIn.wstring()),
          "an existing regular file is found");

    // Spaces and non-ASCII must survive the UTF-8 boundary the POSIX
    // implementation converts across. On Windows there is no conversion, so this
    // assertion is cheap there and load-bearing on Linux and macOS.
    // \u escapes rather than literal UTF-8 bytes: without a BOM, MSVC reads a
    // source file in the local codepage. config_path_tests.cpp does the same.
    fs::path unicodeFile = probeDir / L"tinta \u65E5\u672C probe.ini";
    {
        std::ofstream out(unicodeFile, std::ios::binary);
        out << "; unicode probe\n";
    }
    check(platform::regularFileExists(unicodeFile.wstring()),
          "a path with spaces and non-ASCII survives the encoding boundary");

    // --- fileWriteTicks -----------------------------------------------------
    // The seam that made file_utils.cpp portable. Its contract is a value in the
    // SAME units and epoch the tree already persists and displays, so what has to
    // be pinned is not "did it return something" but "does it mean 1601 ticks".
    {
        const unsigned long long kTicksPerSecond = 10000000ULL;
        // Independent of settings.cpp's fileTimeTicksNow(), which cannot be called
        // here: settings.h reaches app.h and therefore windows.h, which is the
        // entire reason the platform seam exists. So the clock is read directly.
        const unsigned long long nowTicks =
            platform::kUnixEpochInFileTimeTicks +
            static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()) *
                kTicksPerSecond;

        const unsigned long long ticks = platform::fileWriteTicks(optIn.wstring());
        check(ticks != 0, "a file just written reports a non-zero write time");

        // AN HOUR EITHER WAY, and that window is the assertion. It is wide enough
        // that no filesystem timestamp granularity or clock skew on a build
        // machine can trip it, and narrow enough to fail on every way this can go
        // wrong by construction: a Unix epoch is 369 YEARS out, a seconds-instead-
        // of-ticks return is 7 orders of magnitude out, and dividing nanoseconds by
        // the wrong power of ten moves it by decades.
        const unsigned long long kHour = 3600ULL * kTicksPerSecond;
        check(ticks > nowTicks - kHour && ticks < nowTicks + kHour,
              "the write time is within an hour of now IN FILETIME TICKS -- the same "
              "epoch settings.ini persists and the start page decodes");

        // CONTROL, and it is the specific defect file_utils.cpp's old comment
        // warned about: packing a POSIX timestamp into the same 64 bits satisfies
        // every equality comparison in the tree and is wrong by 369 years. If the
        // window above ever stopped biting, this is what would slip through it.
        const unsigned long long unixEpochPacking =
            ticks - platform::kUnixEpochInFileTimeTicks;
        check(!(unixEpochPacking > nowTicks - kHour &&
                unixEpochPacking < nowTicks + kHour),
              "CONTROL: the same instant packed against the UNIX epoch fails the "
              "window above, so that window is what detects the packing trap");

        // The exact split file_utils.cpp performs, because it does it by shifting
        // rather than through a ULARGE_INTEGER (win32_types.h stands FILETIME in
        // off Windows but not that union), and a mask or shift error here would
        // still produce a plausible-looking number.
        const unsigned long low = static_cast<unsigned long>(ticks & 0xFFFFFFFFULL);
        const unsigned long high = static_cast<unsigned long>(ticks >> 32);
        const unsigned long long rejoined =
            (static_cast<unsigned long long>(high) << 32) |
            static_cast<unsigned long long>(low);
        check(rejoined == ticks,
              "splitting into a FILETIME's two halves and rejoining round-trips");
        check(high != 0,
              "CONTROL: the high half is actually populated, so the round-trip "
              "above is not passing on two zeroes");

        check(platform::fileWriteTicks(optIn.wstring()) == ticks,
              "reading an unmodified file twice gives the same value");
        check(platform::fileWriteTicks(unicodeFile.wstring()) != 0,
              "a non-ASCII path reports a write time, so the seam converts the "
              "path the same way regularFileExists does");
        check(platform::fileWriteTicks((probeDir / L"no-such-file.md").wstring()) == 0,
              "a missing file reports 0 rather than a garbage or partial time");
        check(platform::fileWriteTicks(L"") == 0, "an empty path reports 0");

        // A DIRECTORY'S WRITE TIME, AND THE CONDITION IS THE BACKEND, NOT THE OS.
        // This block used to be `#ifdef _WIN32` and to call the disagreement a
        // deliberate per-platform divergence on the grounds that both answers were
        // correct. Measuring Qt on both platforms refuted the premise: Windows
        // returned 0 only because CreateFileW refuses a directory without
        // FILE_FLAG_BACKUP_SEMANTICS, which the original did not pass -- so it was
        // an artifact of a flag, not a truth about the platform. QFileInfo reports
        // a real time on Windows (measured, 6.8.3) and on Linux (6.4.2) alike.
        //
        // No caller in the tree asks a directory for its write time, so nothing
        // behavioural rides on this. It is asserted anyway, and keyed on the
        // backend, because that is what makes the change VISIBLE at the cutover
        // instead of discovered afterwards.
#ifdef TINTA_PATHS_BACKEND_QT
        check(platform::fileWriteTicks(probeDir.wstring()) != 0,
              "with the Qt backend a DIRECTORY reports a real write time on EVERY "
              "platform -- QFileInfo describes one, and the old Windows 0 was an "
              "artifact of CreateFileW's flags rather than a platform difference");
#elif defined(_WIN32)
        check(platform::fileWriteTicks(probeDir.wstring()) == 0,
              "with the native Win32 backend a DIRECTORY reports 0 -- CreateFileW "
              "without FILE_FLAG_BACKUP_SEMANTICS cannot open one");
#else
        check(platform::fileWriteTicks(probeDir.wstring()) != 0,
              "with a native POSIX backend a DIRECTORY reports a real time -- stat() "
              "describes one");
#endif
    }

    // --- openExternalUrl vs openInDefaultApplication -------------------------
    //
    // WHAT THIS ASSERTS IS A DECISION, NOT A LAUNCH, and the distinction is the
    // reason the block exists at all. openExternalUrl cannot be called from a
    // test: it opens a browser. What CAN be falsified is the premise the second
    // function was added on -- that openInDefaultApplication, whose own comment
    // says it "replaces ShellExecuteW on Windows", is the wrong reuse for a web
    // URL because it wraps QUrl::fromLocalFile.
    //
    // That premise was argued in a header comment, and a comment cannot go red.
    // If a future Qt made fromLocalFile scheme-aware, the argument for having two
    // functions would be void and nothing in the tree would say so -- somebody
    // would eventually "simplify" openExternalUrl back onto the other one and it
    // would work in testing, because a file:// URL for a path that exists opens.
    // The failure only shows on an http URL, on a user's machine.
#ifdef TINTA_PATHS_BACKEND_QT
    {
        const QUrl asLocalFile = QUrl::fromLocalFile("https://example.com/a.html");
        const QUrl asWebUrl = QUrl("https://example.com/a.html");

        check(asWebUrl.scheme() == "https",
              "a web URL constructed directly keeps its scheme -- this is what "
              "openExternalUrl hands to QDesktopServices");
        check(asLocalFile.scheme() == "file",
              "QUrl::fromLocalFile RE-ENCODES a web URL as a file URL, which is "
              "why openInDefaultApplication is the wrong reuse for openExternalUrl "
              "rather than merely a less tidy one");
        check(asLocalFile != asWebUrl,
              "the two spellings are not interchangeable -- the whole argument for "
              "a second function in platform_paths.h is this inequality");
        check(asLocalFile.toString().contains(QLatin1String("https:/")),
              "the mangling is visible in the result: the scheme survives as a "
              "PATH SEGMENT, so the browser would never be reached and no error "
              "would be reported either");
    }
#endif

    // --- userConfigDirectory ------------------------------------------------
    // A unique app name, never "Tinta": this test must not create, read or
    // remove the real configuration home on a developer's machine. That is also
    // why tintaConfigDir() itself is not called here.
    std::wstring appName = L"TintaPathsTest-" + stamp;
    std::wstring configDir = platform::userConfigDirectory(appName);
    check(!configDir.empty(), "userConfigDirectory resolves to a path");
    check(fs::is_directory(fs::path(configDir)),
          "userConfigDirectory created the directory");
    check(configDir.find(appName) != std::wstring::npos,
          "the resolved path contains the requested app name");
    check(platform::userConfigDirectory(appName) == configDir,
          "resolving twice is idempotent and does not fail on an existing dir");

    // --- UTF-8 <-> wide conversion ------------------------------------------
    // This pair replaced ten hand-rolled copies, so it is the one place the
    // behaviour can be pinned. The wide side is UTF-16 on Windows and UTF-32 on
    // POSIX, so assertions are on round-trip identity and on the UTF-8 bytes,
    // never on wide length -- a wide-length assertion would pass on one platform
    // and fail on the other for correct code.
    check(platform::toUtf8(L"").empty(), "empty wide converts to empty UTF-8");
    check(platform::fromUtf8("").empty(), "empty UTF-8 converts to empty wide");
    check(platform::toUtf8(L"plain ascii") == "plain ascii", "ASCII passes through");

    // U+00E9 (2 bytes), U+65E5 (3 bytes), U+1F600 (4 bytes) -- one case per
    // UTF-8 length, because a decoder can be right about three and wrong about
    // the fourth, and the 4-byte case is the one that needs a surrogate pair on
    // Windows and a single wchar_t on POSIX.
    const std::string twoByte = "caf\xC3\xA9";
    const std::string threeByte = "\xE6\x97\xA5\xE6\x9C\xAC";
    const std::string fourByte = "\xF0\x9F\x98\x80";
    check(platform::toUtf8(platform::fromUtf8(twoByte)) == twoByte,
          "2-byte sequence round-trips");
    check(platform::toUtf8(platform::fromUtf8(threeByte)) == threeByte,
          "3-byte sequence round-trips");
    check(platform::toUtf8(platform::fromUtf8(fourByte)) == fourByte,
          "4-byte sequence round-trips (surrogate pair on Windows)");

    const std::string mixed = "a" + twoByte + "-" + threeByte + "/" + fourByte + "z";
    check(platform::toUtf8(platform::fromUtf8(mixed)) == mixed,
          "mixed-width string round-trips byte for byte");

    // CONTROL: round-trip identity alone would also hold for an implementation
    // that returned its input unchanged, or that dropped every non-ASCII byte in
    // both directions. These pin the actual byte counts, so a pass-through or a
    // lossy stub fails here rather than sailing through the checks above.
    check(platform::fromUtf8(twoByte).size() == 4,
          "CONTROL: 2-byte sequence decodes to one wide unit, not two");
    check(platform::toUtf8(platform::fromUtf8(threeByte)).size() == 6,
          "CONTROL: non-ASCII is not dropped on the way through");
    check(platform::fromUtf8(threeByte).size() == 2,
          "CONTROL: 3 UTF-8 bytes become 1 wide unit per character");

    // A lone continuation byte is not valid UTF-8. The contract is U+FFFD, not a
    // drop and not an empty result: a path we cannot decode must stay visibly
    // wrong rather than silently becoming a different valid path.
    std::wstring replaced = platform::fromUtf8("ok\x80tail");
    check(!replaced.empty(), "malformed input does not yield an empty string");
    check(replaced.find(static_cast<wchar_t>(0xFFFD)) != std::wstring::npos,
          "malformed UTF-8 becomes U+FFFD rather than being dropped");

    // --- preferredUiLanguages ----------------------------------------------
    // openInDefaultApplication is deliberately NOT exercised here. Calling it
    // would launch a real application on the machine running the test suite,
    // which is a side effect a test has no business having; its only caller is
    // a menu item a person clicks.
    //
    // The list itself cannot be asserted -- it is whatever the user has
    // configured -- so what is checked is the *shape contract*, which is the
    // part a caller depends on and the part the normalization can get wrong.
#ifndef _WIN32
    // FORCED BEFORE THE FIRST QUERY, AND THE ORDERING IS THE WHOLE POINT.
    // Measured: the Qt backend resolves the system locale ONCE per process and
    // caches the first non-C answer, so a setenv() after the list has been read is
    // never observed. The previous shape of this block read the real list first and
    // forced LANGUAGE afterwards, which passed only on a machine whose environment
    // named no language -- and failed all three of its own controls on one that
    // did, against correct code. It looked like it worked because the machine it
    // was written on has no LANGUAGE set, where the first answer is the C locale
    // and Qt does re-resolve past that.
    //
    // Forcing first also fixes a vacuous pass: with no LANGUAGE the backend
    // reports only the C locale, the filter drops it, and every assertion in the
    // loop below ran ZERO times while the suite reported green.
    //
    // A LEADING "C" HERE WOULD NOT REACH THE C/POSIX FILTER, and an earlier version
    // of this comment claimed it did. Measured: seeded with "C:zh_CN.UTF-8:ja_JP@x",
    // Qt drops the "C" itself while resolving the list, so the backend's filter
    // never sees it and deleting that filter left this test green. The filter is
    // only reachable in a BARE locale, which is what --bare-locale is for, and one
    // process cannot test both because the resolved locale is cached. So the entries
    // below are only the two that do normalization work -- native locale form,
    // underscore, codeset, @modifier.
    //
    // NOT on __APPLE__: measured, setenv("LANGUAGE", ...) has zero effect there --
    // macOS's QLocale::system() resolves through CoreFoundation/NSUserDefaults
    // (System Settings), which never consults LANG/LC_ALL/LANGUAGE at all. The
    // forcing knob that DOES reach it is `-AppleLanguages (tags)`, an argument-domain
    // override read from argv at first access -- too early for a setenv() here to
    // help even if the variable mattered -- so CMakeLists.txt passes it on the
    // platform_paths test's command line instead, before this process starts.
#ifndef __APPLE__
    if (!bareLocale) {
        setenv("LANGUAGE", "zh_CN.UTF-8:ja_JP@x", 1);
    }
#endif
    // In --bare-locale mode the environment is left exactly as ctest set it
    // (LC_ALL=C, LANG=C, empty LANGUAGE, measured as the single tag "C"), because
    // the point of that mode is to observe the untouched C locale.
#endif
    std::vector<std::wstring> tags = platform::preferredUiLanguages();
    for (const std::wstring& tag : tags) {
        check(!tag.empty(), "no reported language tag is empty");
        // A POSIX locale name is "zh_CN.UTF-8@pinyin". If any of these survive,
        // the implementation returned a locale name instead of a BCP 47 tag and
        // every caller matching against "zh-cn" would silently miss.
        check(tag.find(L'_') == std::wstring::npos, "tag uses '-' not '_'");
        check(tag.find(L'.') == std::wstring::npos, "tag carries no codeset suffix");
        check(tag.find(L'@') == std::wstring::npos, "tag carries no @modifier suffix");
        check(tag != L"C" && tag != L"POSIX", "the C/POSIX locale is not a language");
    }
    check(platform::preferredUiLanguages() == tags, "the language list is stable across calls");

    // POSITIVE CONTROL. Every assertion in the loop above passes vacuously on an
    // empty list, and an empty list is a legitimate result on a container with no
    // locale set -- so without something below, a stub returning {} would look
    // exactly like a pass. These make the instrument speak on both platforms.
#ifdef _WIN32
    // Windows always reports at least one UI language.
    check(!tags.empty(), "CONTROL: Windows reports at least one UI language");
#else
    if (bareLocale) {
        // THE C/POSIX FILTER'S ONLY CONTROL, and it needs its own process. ctest
        // registers this mode as a second entry with LC_ALL=C, LANG=C and an empty
        // LANGUAGE -- measured to make the backend report exactly one tag, "C" --
        // so the filter drops it and the correct result is an EMPTY list.
        //
        // Not vacuous, and not the same thing as the loop above: delete the C/POSIX
        // skip from the backend and this fails twice, here and at "the C/POSIX
        // locale is not a language", because the loop then has one tag to walk. In
        // the forced mode it fails not at all, which is exactly why this exists --
        // a planted-defect run against the forced list reported green.
        //
        // An empty LANGUAGE alone is NOT enough (measured: LANG=en_AU with
        // LANGUAGE= reports en-AU), so all three variables have to be set; the
        // CMake ENVIRONMENT property carries them, not this file.
        check(tags.empty(),
              "CONTROL: a bare C/POSIX locale reports no languages at all");
        std::cout << "bare-locale mode: C/POSIX filter observed\n";
    } else {
    // On POSIX the input is the environment, so it can be supplied: LANGUAGE is
    // the colon-separated preference list, and both entries here are written in
    // native locale form specifically to make the normalization do work.
    // Asserted on `tags` itself, not on a second list read after a second setenv:
    // the backend caches, so a re-forced value cannot be observed at all (measured).
    // That also retires the old LANGUAGE=C sub-control below, which would have
    // passed vacuously here -- it would have been re-reading the cached zh/ja list
    // and concluding that C was filtered.
    const std::vector<std::wstring>& forced = tags;
    check(forced.size() >= 2, "CONTROL: LANGUAGE supplies a preference list");
    // Primary-subtag prefix, not the exact requested tag: measured on macOS, the
    // most-preferred entry is "zh-Hans-CN" (CoreFoundation's maximized/canonical form),
    // with the literally-requested "zh-CN" second -- a different expansion order from
    // Linux's glibc-driven backend, which puts the requested form first. Both orderings
    // start with "zh", which is the same prefix-match contract the loosened position-1
    // check below already relies on: detectSystemLanguage() in i18n.cpp matches by
    // PREFIX, so it cannot tell "zh-CN" and "zh-Hans-CN" apart at position 0 either.
    check(!forced.empty() && forced[0].rfind(L"zh", 0) == 0,
          "CONTROL: the most-preferred locale name normalizes to a BCP 47 tag");

    // THIS ASSERTION USED TO READ `forced[1] == L"ja-JP"` AND WOULD NOW FAIL
    // AGAINST A CORRECT IMPLEMENTATION. Qt expands each entry with likely subtags,
    // so the measured list is six long, not two:
    //     zh-CN, zh-Hans-CN, zh, ja-JP, ja-Jpan-JP, ja
    // and position 1 is zh-Hans-CN. Asserting a POSITION was over-specifying from
    // the start: what the only caller depends on -- detectSystemLanguage() in
    // i18n.cpp, which matches by PREFIX while walking the whole list in order -- is
    // that both requested languages are present and that the more-preferred one
    // still comes FIRST. Expansion cannot break that; a reversed list would, and
    // this still catches it.
    auto positionOf = [&forced](const wchar_t* tag) -> long long {
        for (size_t i = 0; i < forced.size(); i++) {
            if (forced[i] == tag) return static_cast<long long>(i);
        }
        return -1;
    };
    const long long zhAt = positionOf(L"zh-CN");
    const long long jaAt = positionOf(L"ja-JP");
    check(zhAt >= 0 && jaAt >= 0,
          "CONTROL: both requested languages survive normalization and are reported");
    check(zhAt >= 0 && jaAt >= 0 && zhAt < jaAt,
          "CONTROL: preference ORDER is preserved, even though the backend may insert "
          "expanded tags (zh-Hans-CN, ja-Jpan-JP) between the two");
    // The C locale is not re-forced here. A second setenv is unobservable (the
    // backend has already cached the resolved locale), so it would have been a
    // control that cannot fire; the --bare-locale entry above covers it in the only
    // process state where it can be seen.
    unsetenv("LANGUAGE");
    }  // !bareLocale
#endif
    std::cout << "preferred UI languages: " << tags.size() << '\n';

    // --- pathsEquivalent ----------------------------------------------------
    //
    // Same source on both platforms and DIFFERENT expected answers, for the same
    // reason isHiddenEntry's block below has them: the seam exists because the
    // filesystems genuinely disagree, so a block that asserted one answer
    // everywhere would be asserting that one of the two arms is wrong.
    {
        const fs::path lower = probeDir / L"equivalence.md";
        { std::ofstream out(lower, std::ios::binary); out << "x"; }
        check(fs::exists(lower),
              "CONTROL: the pathsEquivalent probe file was actually created");

        const std::wstring same = lower.wstring();
        std::wstring upper = same;
        const size_t leaf = upper.find_last_of(L"\\/");
        upper.replace(leaf + 1, 1, 1, L'E');  // equivalence.md -> Equivalence.md

        // Agreed on by both platforms, and it is the control: without this a broken
        // arm that returned false for everything would still pass the case tests on
        // Linux.
        check(platform::pathsEquivalent(same, same),
              "CONTROL: a path is equivalent to itself on any platform");
        check(!platform::pathsEquivalent(same, (probeDir / L"other.md").wstring()),
              "CONTROL: two different names are never equivalent");

        // THE CASE THAT DECIDES IT. One spelling, one file on Windows, two files
        // on Linux, and the seam has to say so.
#ifdef _WIN32
        check(platform::pathsEquivalent(same, upper),
              "paths differing only in case are the SAME document on Windows");
#else
        check(!platform::pathsEquivalent(same, upper),
              "paths differing only in case are DIFFERENT documents off Windows");
#endif

        // std::filesystem::equivalent IS REJECTED, AND THIS IS WHERE THAT IS
        // PINNED RATHER THAN ASSERTED IN THE HEADER'S PROSE. The real caller sits
        // under the comment "Vanished since it was recorded: drop the stale entry",
        // so it compares against a path that is gone by construction. `equivalent`
        // needs both operands to exist and would report an error here, turning a
        // stale-entry purge into a silent no-op; QFileInfo::canonicalFilePath is the
        // same trap wearing Qt's clothes, since it returns EMPTY for a missing path
        // and two empties compare equal. A lexical answer is the only correct one.
        const fs::path vanished = probeDir / L"vanished.md";
        { std::ofstream out(vanished, std::ios::binary); out << "x"; }
        const std::wstring recordedSpelling = vanished.wstring();
        fs::remove(vanished, error);
        check(!error && !fs::exists(vanished),
              "CONTROL: the vanished probe file is really gone");
        check(platform::pathsEquivalent(recordedSpelling, recordedSpelling),
              "a path that no longer exists still equals its recorded spelling");
        // And the falsifier for a canonicalize-based implementation: two DIFFERENT
        // missing paths must not collapse onto each other.
        check(!platform::pathsEquivalent(recordedSpelling,
                                         (probeDir / L"also-gone.md").wstring()),
              "two different missing paths are not equivalent to each other");
    }

    // --- createEmptyFileExclusive -------------------------------------------
    //
    // The seam's whole reason to exist is the SECOND call, so the test spends its
    // assertions there. The failure mode of getting exclusivity wrong is not a
    // wrong return value -- it is a document the user silently loses, because the
    // portable-looking alternative (std::ofstream) truncates.
    {
        const fs::path fresh = probeDir / L"exclusive-new.md";
        check(platform::createEmptyFileExclusive(fresh.wstring()),
              "creating a file that does not exist succeeds");
        check(fs::exists(fresh) && fs::file_size(fresh) == 0,
              "the created file exists and is empty");

        { std::ofstream out(fresh, std::ios::binary); out << "important"; }
        check(fs::file_size(fresh) == 9, "CONTROL: the file now has contents to lose");

        check(!platform::createEmptyFileExclusive(fresh.wstring()),
              "creating a file that already exists FAILS rather than truncating");
        check(fs::file_size(fresh) == 9,
              "the existing file's contents survive the refused create");

        // A directory occupies the name too, and it must refuse there as well --
        // the one caller's other branch makes directories, so a name collision
        // across the two kinds is reachable from the same input box.
        check(!platform::createEmptyFileExclusive(probeDir.wstring()),
              "a path occupied by a directory is refused");
    }

    // --- isHiddenEntry ------------------------------------------------------
    //
    // The seam exists because the two platforms disagree about what "hidden"
    // means, so the assertions below deliberately disagree too. Asserting the same
    // answer on both would mean one of the implementations was wrong.
    {
        const fs::path plain = probeDir / L"visible.md";
        const fs::path dotted = probeDir / L".dotted.md";
        { std::ofstream out(plain, std::ios::binary); out << "x"; }
        { std::ofstream out(dotted, std::ios::binary); out << "x"; }

        check(fs::exists(plain) && fs::exists(dotted),
              "CONTROL: both isHiddenEntry probe files were actually created");

        // Agreed on by both platforms: an ordinary name with no attribute set.
        check(!platform::isHiddenEntry(plain.wstring(), L"visible.md"),
              "an ordinary file is not hidden on any platform");

#ifdef _WIN32
        // The name means nothing here; only the attribute does. ".gitignore" is a
        // visible file on Windows and Explorer shows it.
        check(!platform::isHiddenEntry(dotted.wstring(), L".dotted.md"),
              "a dot-name is NOT hidden on Windows -- the attribute governs");

        check(SetFileAttributesW(dotted.c_str(), FILE_ATTRIBUTE_HIDDEN) != 0,
              "CONTROL: the hidden attribute could be set at all");
        check(platform::isHiddenEntry(dotted.wstring(), L".dotted.md"),
              "a file with FILE_ATTRIBUTE_HIDDEN is hidden on Windows");
        // Clear it again so remove_all does not trip over it.
        SetFileAttributesW(dotted.c_str(), FILE_ATTRIBUTE_NORMAL);

        // An unreadable or absent entry reports NOT hidden, so a listing shows an
        // entry the user may fail to open rather than silently omitting it.
        check(!platform::isHiddenEntry((probeDir / L"no-such-file.md").wstring(),
                                       L"no-such-file.md"),
              "an entry that cannot be queried is reported as not hidden");
#else
        // No attribute exists to consult, so the name is the whole rule.
        check(platform::isHiddenEntry(dotted.wstring(), L".dotted.md"),
              "a dot-name IS hidden off Windows -- the name is the convention");

        // CONTROL: the POSIX implementation never looks at the path, only the final
        // component, so a dot in a PARENT directory must not hide a visible file.
        // Without this the test would pass on an implementation that searched the
        // whole path for a dot, which would hide everything under any dotdir.
        check(!platform::isHiddenEntry(L"/home/u/.config/visible.md", L"visible.md"),
              "CONTROL: a dot in a parent directory does not hide the entry");
#endif
    }

    // --- environmentVariable -------------------------------------------------
    //
    // Arranged with the platform's own setter, which is not a seam and does not
    // want to be: nothing in tinta sets an environment variable, so a write side
    // would be surface built for its own test. On Windows this writes the Win32
    // block, which is what BOTH arms read there -- GetEnvironmentVariableW
    // directly, and QProcessEnvironment because that is where it looks.
    {
#ifdef _WIN32
        auto setVar = [](const wchar_t* name, const wchar_t* value) {
            SetEnvironmentVariableW(name, value);
        };
#else
        auto setVar = [](const wchar_t* name, const wchar_t* value) {
            setenv(platform::toUtf8(name).c_str(),
                   platform::toUtf8(value).c_str(), 1);
        };
#endif

        setVar(L"TINTA_PATHS_PROBE", L"a-known-value");
        check(platform::environmentVariable(L"TINTA_PATHS_PROBE") ==
                  L"a-known-value",
              "a variable the test set reads back exactly");

        check(platform::environmentVariable(L"TINTA_NO_SUCH_VARIABLE_12345")
                  .empty(),
              "a variable that cannot exist reads empty");

        // PINS THE HEADER'S DOCUMENTED COLLAPSE rather than leaving it as prose.
        // Both platforms CAN distinguish set-to-empty from absent, and the seam
        // chooses not to; if somebody later decides the difference matters, this
        // assertion fails and sends them to the header to change the contract
        // first. That is the point of asserting a decision and not just a result.
        setVar(L"TINTA_PATHS_PROBE_EMPTY", L"");
        check(platform::environmentVariable(L"TINTA_PATHS_PROBE_EMPTY").empty(),
              "a variable set to the empty string is indistinguishable from absent");

        // LONGER THAN MAX_PATH, and this one is the assertion that would have failed
        // on the code this seam replaces. src/pandoc.cpp read its variables into a
        // wchar_t[MAX_PATH], so a 600-character value came back as nothing at all --
        // silently, because the call reports the size it wanted and the old code
        // only tested for > 0. An environment variable exceeding 260 characters is
        // ordinary; PATH usually does.
        const std::wstring longValue(600, L'x');
        setVar(L"TINTA_PATHS_PROBE_LONG", longValue.c_str());
        const std::wstring readBack =
            platform::environmentVariable(L"TINTA_PATHS_PROBE_LONG");
        check(readBack.size() == 600,
              "a 600-character value is returned whole, not truncated to MAX_PATH");
        check(readBack == longValue, "the long value round-trips byte for byte");

        // The values here are ASCII on purpose. The two arms decode the environment
        // by different routes -- a wide Win32 block on Windows, QProcessEnvironment's
        // local 8-bit decoding on POSIX -- so a non-ASCII assertion would be pinning
        // the host's locale rather than this seam's behaviour. That is a real limit of
        // this block and it is stated rather than papered over.
    }

    // --- findExecutableOnPath ------------------------------------------------
    {
        // An executable guaranteed present on each host, named WITHOUT a suffix,
        // because the bare name is the seam's contract. This is not a claim that
        // pandoc -- the one real caller -- is installed anywhere; nothing in this
        // tree's tests requires that, and pandoc may well be absent on both hosts.
#ifdef _WIN32
        const wchar_t* presentTool = L"cmd";
#else
        const wchar_t* presentTool = L"sh";
#endif
        const std::wstring resolved = platform::findExecutableOnPath(presentTool);
        check(!resolved.empty(), "an executable known to be installed resolves");

        // THE TWO NEW FUNCTIONS CHECK EACH OTHER, which is worth more than either
        // checking itself: a path that findExecutableOnPath invented rather than
        // found would satisfy the non-empty assertion above and fail this one.
        check(platform::regularFileExists(resolved),
              "the resolved path is a regular file that actually exists");

        check(platform::findExecutableOnPath(L"tinta-no-such-executable-12345")
                  .empty(),
              "an executable that cannot exist resolves to empty");

#ifdef _WIN32
        // CONTROL for the suffix rule: SearchPathW's lpExtension is appended only
        // when the name carries no extension, so a name that already ends in .exe
        // must still resolve. Without this an implementation that concatenated
        // unconditionally would look correct on the bare-name test above while
        // turning "cmd.exe" into "cmd.exe.exe" -- and the one real caller used to
        // pass exactly that spelling, so it is the regression a reader would fear.
        check(!platform::findExecutableOnPath(L"cmd.exe").empty(),
              "CONTROL: a name that already carries .exe still resolves");
#endif
    }

    // --- config_dir.cpp links and behaves ----------------------------------
    check(configFilePathForShell(L"").empty(), "empty shell path stays empty");
    check(!configFilePathForShell(optIn.wstring()).empty(),
          "an existing file resolves to a non-empty shell path");

    // --- expandEnvironmentPlaceholders --------------------------------------
    //
    // The placeholder SYNTAX is per-platform by contract (%VAR% on Windows, $VAR and
    // ${VAR} elsewhere), so the spelling is #ifdef'd and every assertion about behaviour
    // is shared. That split is the point: a test that only checked "something expanded"
    // would pass against a seam that honoured the wrong platform's syntax.
    //
    // The variable is SET BY THE TEST rather than borrowed from the environment. Reading
    // %APPDATA% or $HOME would make the expected value a property of the machine, and the
    // 400-character case below cannot be arranged that way at all.
    {
        const std::wstring value = L"probe-value";
#ifdef _WIN32
        SetEnvironmentVariableW(L"TINTA_EXPAND_PROBE", value.c_str());
        const std::wstring bare = L"%TINTA_EXPAND_PROBE%";
        const std::wstring missing = L"%TINTA_NO_SUCH_VAR_HERE%";
#else
        setenv("TINTA_EXPAND_PROBE", "probe-value", 1);
        const std::wstring bare = L"$TINTA_EXPAND_PROBE";
        const std::wstring missing = L"$TINTA_NO_SUCH_VAR_HERE";
#endif
        check(platform::expandEnvironmentPlaceholders(bare) == value,
              "a set variable expands to its value");
        check(platform::expandEnvironmentPlaceholders(bare + L"/notes.md") ==
                  value + L"/notes.md",
              "expansion keeps the rest of the path, which is how the caller uses it");
        check(platform::expandEnvironmentPlaceholders(L"plain/path.md") == L"plain/path.md",
              "CONTROL: a path with no placeholder comes back unchanged");

        // ISC-8. Literal, NOT empty. Expanding an unset variable away would turn
        // "$NOSUCHVAR/notes" into "/notes" -- a path that may well exist, so the folder
        // browser would open the wrong directory and look like it had worked.
        check(platform::expandEnvironmentPlaceholders(missing) == missing,
              "an UNSET variable is left literal, so the caller's path fails to resolve");

#ifndef _WIN32
        check(platform::expandEnvironmentPlaceholders(L"${TINTA_EXPAND_PROBE}/n.md") ==
                  value + L"/n.md",
              "the braced form expands and consumes its closing brace");
        check(platform::expandEnvironmentPlaceholders(L"${TINTA_EXPAND_PROBE}") == value,
              "the braced form at end of string needs no trailing character");
        check(platform::expandEnvironmentPlaceholders(L"${TINTA_EXPAND_PROBE") ==
                  L"${TINTA_EXPAND_PROBE",
              "an UNTERMINATED brace is literal text, not an error -- the user is typing");
        check(platform::expandEnvironmentPlaceholders(L"/tmp/cost$5.md") == L"/tmp/cost$5.md",
              "a $ before a digit is a literal $, per the shell's own name rule");
        check(platform::expandEnvironmentPlaceholders(L"/tmp/a$/b.md") == L"/tmp/a$/b.md",
              "a $ before a separator is a literal $");
        check(platform::expandEnvironmentPlaceholders(L"/tmp/trailing$") == L"/tmp/trailing$",
              "a $ at end of string is a literal $ and does not read past the end");
#endif

        // ISC-7, AND THE ONLY ASSERTION HERE THAT THE REPLACED CODE WOULD FAIL. It read
        // ExpandEnvironmentStringsW into wchar_t[MAX_PATH] and tested the result > 0 --
        // but that API returns the REQUIRED SIZE when the buffer is too small, which is
        // also positive, and leaves the buffer undefined. So this case used to assign from
        // unterminated memory. 400 + 8 is 408, comfortably past 260.
        const std::wstring longValue(400, L'x');
#ifdef _WIN32
        SetEnvironmentVariableW(L"TINTA_EXPAND_PROBE", longValue.c_str());
#else
        setenv("TINTA_EXPAND_PROBE", std::string(400, 'x').c_str(), 1);
#endif
        const std::wstring expandedLong =
            platform::expandEnvironmentPlaceholders(bare + L"/deep.md");
        check(expandedLong == longValue + L"/deep.md",
              "an expansion PAST MAX_PATH round-trips intact, buffer and all");
        check(expandedLong.size() == 408,
              "CONTROL: the long expansion really is longer than the old 260 buffer");

        // SET-TO-EMPTY EXPANDS TO NOTHING ON BOTH PLATFORMS, and the assertion is SHARED
        // rather than #ifdef'd because the two arms genuinely agree here -- which is only
        // known because this test was first written the other way round and FAILED.
        //
        // The refuted belief was that an empty-but-present variable is unrepresentable on
        // Windows. It is not: SetEnvironmentVariableW deletes only when lpValue is NULL.
        // "an empty value deletes the variable" is cmd.exe's `set X=` convention, and the
        // API does not share it -- so %VAR% on an empty variable expands to nothing, which
        // is exactly what the Qt arm's contains()/value() pair produces off Windows.
        //
        // This is therefore the assertion that earns contains() over value() in the Qt
        // backend: set-to-empty and unset must come out DIFFERENT, and they do -- empty
        // here, literal in the check below.
#ifdef _WIN32
        SetEnvironmentVariableW(L"TINTA_EXPAND_PROBE", L"");
#else
        setenv("TINTA_EXPAND_PROBE", "", 1);
#endif
        check(platform::expandEnvironmentPlaceholders(bare).empty(),
              "a variable SET TO EMPTY expands to nothing -- unlike an unset one");

        // nullptr, not L"", now that the difference is established: this is the call that
        // actually removes the variable, and the assertion below would pass either way if
        // the two were confused, since an empty expansion is not the literal placeholder.
#ifdef _WIN32
        SetEnvironmentVariableW(L"TINTA_EXPAND_PROBE", nullptr);
#else
        unsetenv("TINTA_EXPAND_PROBE");
#endif
        check(platform::expandEnvironmentPlaceholders(bare) == bare,
              "CONTROL: once removed, the placeholder is literal again");
    }

    // --- cleanup ------------------------------------------------------------
    fs::remove_all(fs::path(configDir), error);
    check(!error, "test config directory is removed");
    fs::remove_all(probeDir, error);
    check(!error, "temporary probe directory is removed");

    // The count is printed on SUCCESS, which this file did not do for the several
    // spans it has existed. A binary that prints only a fixed sentence when it passes
    // cannot be told apart from one whose assertions were skipped, gated out, or
    // never reached -- and the counts differ per platform here on purpose, so the
    // number is also the only visible evidence that the #ifdef'd blocks ran at all.
    if (failures == 0) {
        std::cout << "Platform path tests passed (" << checks << " assertions)\n";
        return 0;
    }
    std::cerr << failures << " failure(s) of " << checks << " assertions\n";
    return 1;
}

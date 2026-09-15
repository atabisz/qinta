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

int main() {
    namespace fs = std::filesystem;

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

        // A KNOWN DIVERGENCE, asserted per platform rather than skipped, because
        // both answers are correct and the cause is nameable: CreateFileW refuses a
        // directory without FILE_FLAG_BACKUP_SEMANTICS, which the Windows original
        // did not pass and this seam does not add, while stat() is perfectly happy
        // to describe one. No caller in the tree asks for a directory's write time,
        // so this pins the behaviour instead of unifying it.
#ifdef _WIN32
        check(platform::fileWriteTicks(probeDir.wstring()) == 0,
              "on Windows a DIRECTORY reports 0 -- CreateFileW without "
              "FILE_FLAG_BACKUP_SEMANTICS cannot open one");
#else
        check(platform::fileWriteTicks(probeDir.wstring()) != 0,
              "on POSIX a DIRECTORY reports a real time -- stat() describes one, "
              "and this is a deliberate divergence, not a defect");
#endif
    }

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
    // part a caller depends on and the part the POSIX normalization can get
    // wrong.
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
    // On POSIX the input is the environment, so it can be supplied: LANGUAGE is
    // the colon-separated preference list, and both entries here are written in
    // native locale form specifically to make the normalization do work.
    setenv("LANGUAGE", "zh_CN.UTF-8:ja_JP@x", 1);
    std::vector<std::wstring> forced = platform::preferredUiLanguages();
    check(forced.size() >= 2, "CONTROL: LANGUAGE supplies a preference list");
    if (forced.size() >= 2) {
        check(forced[0] == L"zh-CN", "CONTROL: locale name normalizes to a BCP 47 tag");
        check(forced[1] == L"ja-JP", "CONTROL: preference order is preserved");
    }
    setenv("LANGUAGE", "C", 1);
    check(platform::preferredUiLanguages().empty() ||
              platform::preferredUiLanguages()[0] != L"C",
          "CONTROL: the C locale is filtered out rather than reported");
    unsetenv("LANGUAGE");
#endif
    std::cout << "preferred UI languages: " << tags.size() << '\n';

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

    // --- config_dir.cpp links and behaves ----------------------------------
    check(configFilePathForShell(L"").empty(), "empty shell path stays empty");
    check(!configFilePathForShell(optIn.wstring()).empty(),
          "an existing file resolves to a non-empty shell path");

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

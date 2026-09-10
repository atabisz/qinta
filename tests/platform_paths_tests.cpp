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

namespace {

int failures = 0;

void check(bool condition, const char* message) {
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

    // --- config_dir.cpp links and behaves ----------------------------------
    check(configFilePathForShell(L"").empty(), "empty shell path stays empty");
    check(!configFilePathForShell(optIn.wstring()).empty(),
          "an existing file resolves to a non-empty shell path");

    // --- cleanup ------------------------------------------------------------
    fs::remove_all(fs::path(configDir), error);
    check(!error, "test config directory is removed");
    fs::remove_all(probeDir, error);
    check(!error, "temporary probe directory is removed");

    if (failures == 0) {
        std::cout << "Platform path tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}

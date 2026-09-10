// Tests the platform path seam on every platform, and by compiling
// config_dir.cpp into this target it also proves that file builds and links
// without windows.h. That second point is the pilot's real claim, so it is
// measured here rather than asserted in a comment.

#include "config_dir.h"
#include "platform_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

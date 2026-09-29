#include "config_dir.h"
#include "platform_paths.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// Portable mode (#147): a settings.ini beside the executable makes that
// folder the configuration home - settings, themes.ini, languages.ini,
// and drafts all live there, so the whole setup travels with the exe.
// Without it, everything stays in the per-user config directory as before
// (Store installs land there automatically: their folder is read-only, so
// an exe-adjacent settings.ini cannot exist). Resolved once per run.
// Lives in its own translation unit so the test targets that compile
// themes.cpp or i18n.cpp can link it without dragging in settings.cpp.
//
// The two OS lookups this needs are behind platform_paths.h, so the policy
// below -- prefer the exe folder, else the user config home -- is now the same
// code on every platform rather than being expressed in Win32 calls.
namespace {
std::wstring g_configDir;
bool g_configDirResolved = false;
}  // namespace

void pinTintaConfigDir(const std::wstring& dir) {
    g_configDir = dir;
    g_configDirResolved = true;
}

std::wstring tintaConfigDir() {
    if (g_configDirResolved) return g_configDir;
    g_configDirResolved = true;

    std::wstring exeDir = platform::executableDirectory();
    if (!exeDir.empty()) {
        std::wstring optIn = exeDir + platform::pathSeparator() + L"settings.ini";
        // regularFileExists, not a plain existence test: a *directory* named
        // settings.ini must not switch the app into portable mode.
        if (platform::regularFileExists(optIn)) {
            g_configDir = exeDir;
            return g_configDir;
        }
    }

    g_configDir = platform::userConfigDirectory(L"Tinta");
    return g_configDir;
}

#ifdef _WIN32

std::wstring configFilePathForShell(const std::wstring& path) {
    if (path.empty()) return path;
    // Resolve the file, not just its parent: MSIX can merge redirected
    // files with existing, unvirtualized files in the same AppData folder.
    HANDLE file = CreateFileW(path.c_str(), 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return path;

    DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, VOLUME_NAME_DOS);
    std::wstring resolved(size, L'\0');
    DWORD length = size ? GetFinalPathNameByHandleW(file, resolved.data(), size,
                                                  VOLUME_NAME_DOS) : 0;
    CloseHandle(file);
    if (!length || length >= size) return path;
    resolved.resize(length);

    // Shell file associations expect ordinary DOS/UNC paths, not the
    // extended-path prefix returned by GetFinalPathNameByHandleW.
    if (resolved.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        return L"\\\\" + resolved.substr(8);
    }
    if (resolved.rfind(L"\\\\?\\", 0) == 0 && resolved.size() > 6 &&
        resolved[5] == L':') {
        resolved.erase(0, 4);
    }
    return resolved;
}

#else

// No Store/MSIX filesystem virtualization outside Windows, so there is nothing
// to resolve: the path the caller holds already names the real file. An #ifdef
// in a leaf function rather than a third seam method, because this is genuinely
// one platform's mechanism and not a capability every platform provides.
std::wstring configFilePathForShell(const std::wstring& path) {
    return path;
}

#endif

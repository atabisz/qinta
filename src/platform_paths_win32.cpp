#include "platform_paths.h"

#include <windows.h>
#include <shlobj.h>

// Win32 implementation of the platform path seam. Every call here is one the
// tree already made from config_dir.cpp, kept identical on purpose: this file
// moves code across a boundary and must not change Windows behaviour while
// doing it. SHGetFolderPathW rather than SHGetKnownFolderPath for the same
// reason -- the deprecated call is what shipped, and swapping it is a separate
// decision with its own testing.

namespace platform {

wchar_t pathSeparator() {
    return L'\\';
}

std::wstring executableDirectory() {
    // MAX_PATH matches the buffer the replaced code used. A longer path failed
    // there too, so this is not a new limitation -- but it is a known one, and
    // the port is the right time to revisit it rather than now.
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return std::wstring();

    std::wstring dir = exePath;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return dir.substr(0, slash);
}

bool regularFileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring userConfigDirectory(const std::wstring& appName) {
    wchar_t appDataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        return std::wstring();
    }

    std::wstring path = appDataPath;
    path += L'\\';
    path += appName;
    CreateDirectoryW(path.c_str(), nullptr);  // harmless if it already exists
    return path;
}

} // namespace platform

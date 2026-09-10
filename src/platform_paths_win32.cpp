#include "platform_paths.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

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

// An explicit length is passed rather than -1, so an embedded NUL is carried
// through instead of truncating the string, and the result needs no terminator
// trimming. WC_ERR_INVALID_CHARS is deliberately NOT set: the seam's contract is
// that malformed input degrades to U+FFFD, which is what the default does, and
// failing the call instead would lose the whole string rather than one character.
std::wstring fromUtf8(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                     nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], length);
    return out;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                     nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string out((size_t)length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &out[0], length,
                        nullptr, nullptr);
    return out;
}

std::vector<std::wstring> preferredUiLanguages() {
    // GetUserPreferredUILanguages hands back a double-NUL-terminated list of
    // BCP 47 tags ("en-US\0zh-Hans-CN\0\0"), which is already the shape this
    // seam promises -- so unlike the POSIX side there is nothing to normalize.
    std::vector<std::wstring> tags;
    ULONG count = 0;
    ULONG bufferSize = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &bufferSize)) {
        return tags;
    }
    std::wstring buffer(bufferSize, L'\0');
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &bufferSize)) {
        return tags;
    }

    const wchar_t* p = buffer.c_str();
    for (ULONG i = 0; i < count; ++i) {
        std::wstring tag(p);
        // The count and the NULs are two separate sources of truth for where
        // the list ends. Trusting only the count walks off the end of the data
        // if they ever disagree, so an empty tag stops the walk as well.
        if (tag.empty()) break;
        tags.push_back(tag);
        p += tag.size() + 1;
    }
    return tags;
}

void openInDefaultApplication(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

bool documentPathsEqual(const std::string& a, const std::string& b) {
    // NTFS and the shell treat paths case-insensitively, so this folds case --
    // which is what the _stricmp calls it replaces did.
    //
    // The fold is ASCII-only, and that is deliberate rather than a shortcut.
    // _stricmp folds according to LC_CTYPE, and tinta never calls setlocale, so
    // it runs in the "C" locale where _stricmp folds ASCII and nothing else.
    // These paths are UTF-8, so every non-ASCII byte is >= 0x80 and both the old
    // and the new code compare it verbatim: identical results on every input the
    // tree can produce. A future setlocale() call elsewhere would change what
    // _stricmp does and would NOT change this, which is the better direction --
    // path identity should not depend on the user's locale.
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

} // namespace platform

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

bool pathsEquivalent(const std::wstring& a, const std::wstring& b) {
    // _wcsicmp, not _stricmp on the UTF-8 forms the callers used to hold. The header
    // states the consequence: folding now reaches past ASCII, which is what NTFS does.
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring environmentVariable(const std::wstring& name) {
    // Sized in two calls rather than into a fixed buffer, and this is the ONE
    // place in this file that deliberately does not preserve the behaviour it
    // replaces. src/pandoc.cpp read its variables into a MAX_PATH buffer, so a
    // value longer than 259 characters came back as nothing at all -- and that is
    // not hypothetical for an environment variable, where PATH alone routinely
    // exceeds it. The change is safe in one direction only, which is why it is
    // acceptable here: every caller uses the value to build a path and then asks
    // whether that path exists, so a longer value can only turn a miss into a
    // hit, never a hit into a miss.
    //
    // The first call asks for the size and includes the terminator; the second
    // returns the count written and excludes it. A `written >= needed` is the
    // variable having grown between the two calls, and is reported as absent
    // rather than truncated.
    DWORD needed = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (needed == 0) return std::wstring();
    std::wstring value(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(name.c_str(), value.data(), needed);
    if (written == 0 || written >= needed) return std::wstring();
    value.resize(written);
    return value;
}

std::wstring findExecutableOnPath(const std::wstring& name) {
    // lpExtension is what makes the header's bare-name contract work without this
    // file having to test for a suffix: SearchPathW appends L".exe" only when
    // `name` carries no extension of its own. Passing nullptr there -- which is
    // what src/pandoc.cpp did, spelling L"pandoc.exe" at the call site instead --
    // is the same lookup with the platform's convention moved into portable code.
    //
    // MAX_PATH for the same reason executableDirectory above gives, and with the
    // same honesty: it is the limit the replaced code had, so nothing regresses,
    // but a longer install path fails here and revisiting it is its own change.
    // A return of MAX_PATH or more is SearchPathW reporting the size it wanted,
    // not a path, so it is read as not-found rather than copied out.
    wchar_t found[MAX_PATH]{};
    DWORD len =
        SearchPathW(nullptr, name.c_str(), L".exe", MAX_PATH, found, nullptr);
    if (len == 0 || len >= MAX_PATH) return std::wstring();
    return std::wstring(found, len);
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

unsigned long long fileWriteTicks(const std::wstring& path) {
    // CreateFileW + GetFileTime, not the shorter GetFileAttributesExW, because
    // this is the call file_utils.cpp made and the seam's job is to move it
    // without changing what Windows reports. FILE_SHARE_WRITE is not incidental
    // either: the file whose write time is wanted is very often one another
    // process is in the middle of saving, which is the whole reason to ask.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;

    FILETIME write{};
    const BOOL ok = GetFileTime(h, nullptr, nullptr, &write);
    CloseHandle(h);
    if (!ok) return 0;

    // The original ignored GetFileTime's return value and passed the member's
    // address straight in, so a failure there left a half-written FILETIME. That
    // is not preserved -- it is the one behaviour change here, it is a fix, and
    // it is stated rather than smuggled: the caller now decides what an
    // unreadable time means, and it cannot be handed a partial one.
    ULARGE_INTEGER ticks;
    ticks.LowPart = write.dwLowDateTime;
    ticks.HighPart = write.dwHighDateTime;
    return ticks.QuadPart;
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

void openExternalUrl(const std::string& url) {
    // Byte-for-byte what utils.cpp::openUrl did, including the A suffix and the
    // empty-string guard. See the header for why the A form is preserved rather
    // than upgraded to ShellExecuteW while it is being moved.
    if (url.empty()) return;
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void revealInFileManager(const std::wstring& path) {
    // Byte-identical to the CTX_REVEAL body this replaced, including the quoting:
    // /select, takes ONE argument and a document path routinely contains spaces,
    // so the quotes are load-bearing rather than defensive.
    std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr,
                  SW_SHOWNORMAL);
}

bool createEmptyFileExclusive(const std::wstring& path) {
    // Byte-for-byte the CreateFileW call this moved out of input.cpp's new-note
    // handler, including the zero share mode: the file is closed immediately, so
    // sharing only governs the instant between create and close, and widening it
    // would be a change with no caller asking for one.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
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

bool isHiddenEntry(const std::wstring& fullPath, const std::wstring& name) {
    // The attribute is authoritative on Windows and the spelling is irrelevant:
    // ".gitignore" is a perfectly ordinary visible file here, and Explorer shows
    // it. This is exactly the FILE_ATTRIBUTE_HIDDEN test file_utils.cpp used to
    // make inline against WIN32_FIND_DATAW, so Windows behaviour is unchanged.
    (void)name;
    const DWORD attrs = GetFileAttributesW(fullPath.c_str());
    // Unreadable entry. "Not hidden" is the better failure: showing an entry the
    // user then cannot open is a visible, explicable error, whereas silently
    // omitting it makes a file that exists look like it does not.
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
}

} // namespace platform

#include "platform_paths.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// POSIX implementation of the platform path seam, covering Linux and macOS.
// The two differ in exactly two places -- how the executable path is obtained,
// and where per-user configuration lives -- so they share this file rather than
// getting one each.

namespace platform {

// UTF-8 <-> wchar_t conversion. std::wstring_convert is deprecated in C++17, so
// this is hand-rolled rather than pulled from <codecvt>. wchar_t is 4 bytes on
// both Linux and macOS, so one code point always fits in one unit here; that
// assumption does NOT hold on Windows, which is why this file is POSIX-only.
// Malformed input becomes U+FFFD rather than being dropped: a path we cannot
// decode should fail a later existence check visibly, not silently become a
// different valid path.
std::wstring fromUtf8(const std::string& in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        unsigned char lead = static_cast<unsigned char>(in[i]);
        char32_t cp = 0;
        size_t extra = 0;
        if (lead < 0x80) {
            cp = lead;
            extra = 0;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1Fu;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0Fu;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07u;
            extra = 3;
        } else {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }

        if (i + extra >= in.size()) {  // truncated sequence at end of input
            out.push_back(static_cast<wchar_t>(0xFFFD));
            break;
        }

        bool valid = true;
        for (size_t k = 1; k <= extra; ++k) {
            unsigned char cont = static_cast<unsigned char>(in[i + k]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        if (!valid) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }

        out.push_back(static_cast<wchar_t>(cp));
        i += extra + 1;
    }
    return out;
}

std::string toUtf8(const std::wstring& in) {
    std::string out;
    out.reserve(in.size());
    for (wchar_t wide : in) {
        char32_t cp = static_cast<char32_t>(wide);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

namespace {

std::string executablePathUtf8() {
#if defined(__APPLE__)
    // First call reports the required size and returns -1; that is success here.
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return std::string();
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return std::string();
    buffer.resize(std::strlen(buffer.c_str()));
    return buffer;
#else
    // /proc/self/exe is Linux's authoritative answer. argv[0] is not: it is
    // whatever string the caller chose, and need not be a path at all.
    std::string buffer(4096, '\0');
    ssize_t written = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (written <= 0) return std::string();
    buffer.resize(static_cast<size_t>(written));
    return buffer;
#endif
}

// mkdir -p. EEXIST counts as success so that resolving the config home twice in
// one run is not an error.
bool makeDirectories(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        std::string partial = path.substr(0, i);
        if (mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

} // namespace

wchar_t pathSeparator() {
    return L'/';
}

std::wstring executableDirectory() {
    std::string exe = executablePathUtf8();
    if (exe.empty()) return std::wstring();

    size_t slash = exe.find_last_of('/');
    if (slash == std::string::npos) return std::wstring();
    return fromUtf8(exe.substr(0, slash));
}

bool regularFileExists(const std::wstring& path) {
    struct stat info {};
    if (stat(toUtf8(path).c_str(), &info) != 0) return false;
    return S_ISREG(info.st_mode);
}

std::wstring userConfigDirectory(const std::wstring& appName) {
    const char* home = std::getenv("HOME");
    std::string base;

#if defined(__APPLE__)
    if (home == nullptr || *home == '\0') return std::wstring();
    base = std::string(home) + "/Library/Application Support";
#else
    // Per the XDG base directory spec, a relative or empty XDG_CONFIG_HOME must
    // be ignored rather than used, so the test is for a leading '/'.
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg == '/') {
        base = xdg;
    } else if (home != nullptr && *home != '\0') {
        base = std::string(home) + "/.config";
    } else {
        return std::wstring();
    }
#endif

    std::string full = base + "/" + toUtf8(appName);
    if (!makeDirectories(full)) return std::wstring();
    return fromUtf8(full);
}

} // namespace platform

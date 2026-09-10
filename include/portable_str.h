#ifndef TINTA_PORTABLE_STR_H
#define TINTA_PORTABLE_STR_H

// Case-insensitive string comparison has no portable spelling: MSVC and the
// Windows CRT provide _strnicmp, POSIX provides strncasecmp, and neither is
// available under the other. The tree currently uses the Windows names at 19
// sites (_stricmp, _strnicmp, _wcsicmp, _wcsnicmp); only the ones reached by the
// platform-independent targets are routed through here so far. Files still
// building Windows-only keep calling the CRT directly, and will adopt this
// header as they are ported rather than in one sweep.

#include <cstddef>

#ifdef _WIN32
#include <string.h>
#else
#include <strings.h>
#endif

namespace tinta {

// Same contract as _strnicmp/strncasecmp: <0, 0 or >0, comparing at most n
// bytes without regard to case.
inline int strnicmp(const char* a, const char* b, std::size_t n) {
#ifdef _WIN32
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

}  // namespace tinta

#endif

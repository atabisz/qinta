#ifndef TINTA_PORTABLE_STR_H
#define TINTA_PORTABLE_STR_H

// STRING CALLS WITH NO PORTABLE SPELLING.
//
// The bar for adding something here is that the standard does NOT already cover
// it. That bar excludes more than it admits: `_countof` looks like it belongs and
// does not, because `std::size` is C++17 and this tree builds `-std=gnu++17`, so
// those sites convert outright rather than gaining an indirection. A seam that
// wraps a solved problem hides the fact that it was solved.
//
// Case-insensitive comparison clears the bar: MSVC and the Windows CRT provide
// _strnicmp, POSIX provides strncasecmp, and neither is available under the other.
// The tree names the four Windows spellings (_stricmp, _strnicmp, _wcsicmp,
// _wcsnicmp) at 11 sites in code -- an earlier version of this comment said 19,
// which was a count of occurrences including comments, and is the reason counts in
// this tree get measured with comment lines excluded.
//
// Only the sites reached by the platform-independent targets are routed through
// here so far. Files still building Windows-only keep calling the CRT directly and
// adopt this header as they are ported, rather than in one sweep -- so a name
// appearing in this file is not a claim that every site uses it.

#include <cstdarg>
#include <cstddef>
#include <cwchar>

#ifdef _WIN32
#include <string.h>
#else
#include <cstdlib>
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

// --- Bounded wide formatting ---------------------------------------------------
//
// THE CONTRACT IS "IT MUST FIT, OR THE PROCESS DIES", AND THAT IS MEASURED RATHER
// THAN CHOSEN. A truncating swprintf_s does not return an error on Windows: it
// invokes the invalid-parameter handler, which fail-fasts. Formatting a 10-char
// result into a 4-wchar_t buffer under MSVC 2022 terminates the process with
// 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN) and never reaches the next statement.
// So there is no Windows return value for the truncation case to be compatible
// with, and the honest off-Windows path is the one that keeps the same contract.
//
// Hence the abort below. The tempting alternative -- return the negative value
// glibc gives and let the caller continue -- would be a BEHAVIOUR CHANGE smuggled
// in under a portability seam: 25 of the 26 call sites ignore the return value, so
// off Windows they would quietly carry on with a truncated string where Windows
// would have died. glibc, incidentally, does not leave the buffer indeterminate as
// the standard permits; it writes a NUL-terminated prefix ("ABC" in the probe
// above). That is worth knowing and is not worth depending on.
//
// The one deliberate asymmetry: this aborts on ANY negative return, whereas
// Windows returns -1 without dying for a genuine encoding error. Both are
// unreachable for the `%d`/`%u`/`%ls`/`%s` formats and well-formed inputs this
// tree actually uses, and the strict direction is the safe one. The only site that
// reads the result at all is editor.cpp's line-number gutter, whose `len <= 0`
// guard is therefore dead off Windows -- kept because it is correct on Windows.
//
// Truncation cannot be asserted from inside a test suite on either platform, since
// it ends the process on both by design. It was probed with a standalone
// executable per platform instead; the suite asserts the cases that return.
namespace detail {

inline int vswprintfSeam(wchar_t* buf, std::size_t n, const wchar_t* fmt, va_list args) {
#ifdef _WIN32
    return ::vswprintf_s(buf, n, fmt, args);
#else
    const int r = ::vswprintf(buf, n, fmt, args);
    if (r < 0) {
        // Matches the Windows invalid-parameter fail-fast described above.
        std::abort();
    }
    return r;
#endif
}

}  // namespace detail

// BOTH ARITIES EXIST BECAUSE THE TREE USES BOTH, WHICH THE CENSUS FOUND RATHER
// THAN ASSUMED. Of 26 swprintf_s sites, 16 pass an explicit element count and 10
// rely on MSVC's array-deducing template overload, which takes no count at all.
// The count form maps onto C99 swprintf; the array form has no portable spelling
// whatsoever, so a seam offering only the first would have left 10 sites
// unportable while looking complete.
inline int swprintf(wchar_t* buf, std::size_t n, const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int r = detail::vswprintfSeam(buf, n, fmt, args);
    va_end(args);
    return r;
}

template <std::size_t N>
inline int swprintf(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int r = detail::vswprintfSeam(buf, N, fmt, args);
    va_end(args);
    return r;
}

}  // namespace tinta

#endif

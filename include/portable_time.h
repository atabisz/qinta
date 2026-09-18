#ifndef TINTA_PORTABLE_TIME_H
#define TINTA_PORTABLE_TIME_H

// TIME CALLS WITH NO PORTABLE SPELLING. Currently exactly one.
//
// THIS HEADER EXISTS BECAUSE A PREDICTION IN ANOTHER FILE CAME TRUE, and that is the
// whole justification -- it is not a header created in anticipation. src/signals.cpp
// wrote its own five-line localtime_s/localtime_r split and said, in the comment above
// it, that "a seam earns its own header when the decision recurs across files" and
// that a portable_time.h at that moment "would be a header with one caller and a
// second place to look". That was correct. src/startpage.cpp's relativeWhen is the
// SECOND caller, so the condition signals.cpp itself named is now met, and both files
// route through here rather than one file keeping a private copy.
//
// The bar is the same one portable_str.h states: the standard must NOT already cover
// it. `std::localtime` is standard and is still rejected, because it returns a pointer
// into shared storage that the next call overwrites -- so the thing being wrapped is
// not "converting a time_t" but "converting a time_t REENTRANTLY", which genuinely has
// no portable spelling. The two implementations differ in ARGUMENT ORDER, which is why
// this cannot be a one-line alias or a macro.
//
// WHAT IS DELIBERATELY NOT HERE. No strftime wrapper and no date formatter: the one
// site that formats a date wants the USER'S LOCALE format, which on Windows means
// GetDateFormatEx and off it means a C-locale strftime, and those two do not produce
// the same string. Hiding that behind a shared name would make a real behavioural
// divergence look like a solved portability problem. It stays as a visible #ifdef at
// its single call site. No tick-epoch conversion either -- platform_paths.h already
// owns the 1601-to-1970 offset as kUnixEpochInFileTimeTicks, and a second home for it
// is the kind of duplicate that goes wrong silently.

#include <ctime>

namespace tinta {

// Local wall-clock breakdown of `t`, reentrantly. Returns a zeroed std::tm if the
// conversion fails, which both platforms signal the same way (a null/failed result)
// even though they spell the call differently.
//
// LOCAL rather than UTC at both call sites, deliberately: the results are read by a
// human beside their own clock -- a tray timestamp and a "yesterday" label -- so UTC
// would be a behaviour change and not a portability one.
inline std::tm localTime(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    // Returns errno_t, 0 on success, and leaves the struct indeterminate otherwise --
    // hence the zero-initialisation above rather than only in the failure branch.
    if (localtime_s(&out, &t) != 0) out = std::tm{};
#else
    if (!localtime_r(&t, &out)) out = std::tm{};
#endif
    return out;
}

}  // namespace tinta

#endif  // TINTA_PORTABLE_TIME_H

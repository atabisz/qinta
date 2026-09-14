#ifndef TINTA_WIN32_TYPES_H
#define TINTA_WIN32_TYPES_H

// THE WIN32 VALUE TYPES TINTA'S HEADERS PASS AROUND, IN ONE PLACE.
//
// On Windows this is `#include <windows.h>` and nothing else. Off Windows it
// declares the handful of Win32 types that appear BY VALUE in tinta's own
// headers, which forward declarations cannot cover -- a `HWND` parameter or an
// embedded `RECT` needs a definition, not a promise that one exists.
//
// WHY IT IS A FILE AND NOT SIX COPIES
//
// Measured 2026-09-14, after app.h stopped requiring windows.h to be declared:
// the gate did not disappear, it MOVED. Five sibling headers held the identical
// one-line problem -- editor.h and tabs.h and annotations.h and export.h and
// input.h each did `#include "app.h"` then `#include <windows.h>` -- and between
// them they gated 15 source files. Their needs turned out to be trivial: 256 lines
// total, ZERO COM interfaces, ZERO Win32 API calls, ZERO inline dereferences, and
// only three distinct types between them (HWND, WPARAM, LPARAM).
//
// So the fix is three typedefs, and the only real design question is where they
// live. Six copies is not an option: repeating `typedef struct HWND__* HWND;` is
// legal, but repeating `struct RECT { ... };` is an ODR violation the compiler
// will reject in any translation unit that includes two of these headers -- which
// is most of them. One header, one definition.
//
// WHAT THIS IS NOT
//
// It is not a compatibility layer and it is not the beginning of one. There are no
// functions here, and there will not be: a stand-in for `SetTimer` that silently
// did nothing while looking like it worked is the exact failure this tree keeps
// cataloguing. Function seams live one layer up in win32_api.h, which includes this
// header and states its own rule -- nothing there pretends to work, and where a
// caller already handles failure the off-Windows path fails rather than no-ops.
// Keeping the two apart is what makes that rule enforceable: a reviewer can accept
// this file on sizeof/offsetof evidence alone, and has to think about that one.
//
// WHAT MAY BE ADDED
//
// Two populations, and the admission test differs because the reasons differ.
//
// STRUCTS AND HANDLES: adding one is a claim that some tinta header uses it BY
// VALUE. If a pointer would do, forward-declare it at the point of use instead,
// so that an inline dereference added later fails loudly rather than compiling.
//
// SCALAR SPELLINGS: `UINT32` is not a Win32 concept, it is the SDK's name for a
// 32-bit unsigned integer, and the same goes for FLOAT and BOOL. These appear in
// source-file BODIES rather than in headers, so the by-value-in-a-header test does
// not apply to them -- and unlike a handle there is no forward declaration to
// prefer, because a typedef cannot be forward-declared. They live here because the
// SDK-vs-stand-in comparison below is the apparatus they need and it exists in this
// file only; a second header would have to duplicate the whole `#ifdef _WIN32 ->
// #include <windows.h>` branch to get at the real definitions.
//
// The measured need, 2026-09-14, across all 23 source files that fail off Windows
// (excluding the two backends behind a deliberate #error): UINT32 is 68 of the 242
// undeclared-identifier errors, FALSE is 22, BOOL 2, FLOAT 2, TRUE 0 -- TRUE is
// here anyway because a census of the SOURCE finds 9 uses of it, in files that
// currently die at a missing header before the compiler ever reaches them. Nothing
// else was added: UINT, DWORD, INT32, UINT8, UINT16 and UINT64 appear in NO error
// on any file, so declaring them would be a claim with nothing behind it.
//
// Deliberately NOT here, and this is the load-bearing omission: HRESULT, SUCCEEDED,
// FAILED and S_OK. Supplying them was measured too, and it buys 9 errors out of 359
// -- all of them in the four files whose COM code is supposed to move onto the
// portable text seam instead. Nine errors is not worth letting COM-shaped code
// COMPILE off Windows, which is exactly the "it looked like it worked" failure this
// file's other rule exists to prevent. `SUCCEEDED(x)` becomes `x` when a site moves
// to the seam; text_api.h has already written that down for 8 of the 13 sites.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The stand-ins below are pinned to the real SDK types rather than trusted to
// match them, in the same spirit as keymap.h's VK_ assertions: the definitions
// off Windows are guesses about this SDK's layout, and a guess that is never
// compared is not a fact. These comparisons can only run where the real headers
// are present, so this is the only place they can live.
//
// They matter because the two definitions can meet. A portable-built object file
// and a Windows-built one linked together would disagree about the size of a RECT
// with no diagnostic anywhere -- the same class of silent mismatch render_api.h
// documents at length for the DrawText macro.
static_assert(sizeof(WPARAM) == sizeof(std::uintptr_t),
              "off-Windows WPARAM stands in as uintptr_t; this SDK disagrees");
static_assert(sizeof(LPARAM) == sizeof(std::intptr_t),
              "off-Windows LPARAM stands in as intptr_t; this SDK disagrees");
static_assert(sizeof(RECT) == 4 * sizeof(long),
              "off-Windows RECT stands in as four longs; this SDK disagrees");
static_assert(sizeof(POINT) == 2 * sizeof(long),
              "off-Windows POINT stands in as two longs; this SDK disagrees");
static_assert(offsetof(RECT, left) == 0 && offsetof(RECT, top) == sizeof(long) &&
                  offsetof(RECT, right) == 2 * sizeof(long) &&
                  offsetof(RECT, bottom) == 3 * sizeof(long),
              "off-Windows RECT stands in with left/top/right/bottom in that order");
static_assert(offsetof(WINDOWPLACEMENT, length) == 0 &&
                  offsetof(WINDOWPLACEMENT, ptMinPosition) == 3 * sizeof(unsigned int) &&
                  sizeof(WINDOWPLACEMENT) ==
                      3 * sizeof(unsigned int) + 2 * sizeof(POINT) + sizeof(RECT),
              "off-Windows WINDOWPLACEMENT stands in with the SDK's field order and size");
static_assert(WM_APP == 0x8000, "off-Windows WM_APP stands in as 0x8000");

// The scalar spellings, pinned the same way. is_same rather than sizeof, because
// these are the cases where the exact type is checkable and a matching size is not
// the same fact: `long` and `int` are both 4 bytes here, so a sizeof-only assertion
// would pass on a stand-in that changed overload resolution and printf conversions.
static_assert(std::is_same<UINT32, std::uint32_t>::value,
              "off-Windows UINT32 stands in as std::uint32_t; this SDK disagrees");
static_assert(std::is_same<FLOAT, float>::value,
              "off-Windows FLOAT stands in as float; this SDK disagrees");
static_assert(std::is_same<BOOL, int>::value,
              "off-Windows BOOL stands in as int; this SDK disagrees");
static_assert(TRUE == 1 && FALSE == 0,
              "off-Windows TRUE and FALSE stand in as 1 and 0; this SDK disagrees");

#else  // !_WIN32

#include <cstdint>

// An opaque handle. Deliberately a pointer to an incomplete struct, exactly as the
// SDK declares it under STRICT, so that a `HWND` cannot be silently confused with
// an int and cannot be dereferenced.
struct HWND__;
typedef struct HWND__* HWND;

// The message parameters. UINT_PTR and LONG_PTR in the SDK, which are pointer-sized
// by definition, so the stdint spellings are exact rather than approximate.
typedef std::uintptr_t WPARAM;
typedef std::intptr_t LPARAM;

struct FILETIME {
    unsigned long dwLowDateTime;
    unsigned long dwHighDateTime;
};

struct RECT {
    long left, top, right, bottom;
};

struct POINT {
    long x, y;
};

// Field-for-field with the SDK's, because App stores one by value and zen mode
// round-trips it through settings.ini.
struct WINDOWPLACEMENT {
    unsigned int length;
    unsigned int flags;
    unsigned int showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT rcNormalPosition;
};

// The real value, so the WM_APP_* ids app.h derives from it keep the numbers they
// have on Windows. They are only ever compared against each other, but matching
// the SDK keeps a debugger session on either platform reading the same.
#define WM_APP 0x8000

// The SDK's names for plain scalars. Nothing Win32 about any of them except the
// spelling, which is why they can stand in exactly rather than approximately --
// and why the Windows branch above compares each one against the real SDK type
// instead of trusting these lines.
typedef std::uint32_t UINT32;
typedef float FLOAT;

// BOOL is `int`, not `bool`, and the width is the point: it is 4 bytes, it is what
// DirectWrite's out-parameters are declared as, and text_api.h's isText/isTrimmed
// fields are int32_t for precisely this reason -- a 1-byte `bool` there breaks the
// sizeof/offsetof guard meant to catch a widened field.
typedef int BOOL;
#define TRUE 1
#define FALSE 0

#endif  // _WIN32

#endif  // TINTA_WIN32_TYPES_H

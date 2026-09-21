#ifndef TINTA_WIN32_TYPES_H
#define TINTA_WIN32_TYPES_H

// THE WIN32 VALUE TYPES TINTA'S HEADERS PASS AROUND, IN ONE PLACE.
//
// On Windows this is `#include <windows.h>` plus WIN32_LEAN_AND_MEAN, NOMINMAX and
// three standard headers the stand-ins below are pinned against -- see :85-95, which
// is the authority for this sentence. It read "and nothing else" until 2026-09-19,
// when src/pandoc.cpp QUOTED that phrase to justify dropping its own <shlobj.h> and
// the quote was checked against the code. An inaccurate self-description is cheap
// until something cites it; then it is a false premise in another file. Off Windows it
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
// else was added: DWORD, INT32, UINT8, UINT16 and UINT64 appear in NO error on any
// file, so declaring them would be a claim with nothing behind it. (DWORD: SPENT
// 2026-09-20, corrected two paragraphs down. INT32, UINT8, UINT16 and UINT64 still
// hold.)
//
// UINT was on that list and is CORRECTED IN PLACE rather than removed, because the
// sentence went stale in the interesting direction: UINT later showed up in two
// render.cpp errors, which by the reasoning above would have argued for adding it.
// It was not added. The bar for this header is that the STANDARD does not already
// cover the thing, and `unsigned` covers UINT completely -- so the fix was to spell
// the two struct members `unsigned`, matching tinta::DecodedImage, which is where
// their values come from. A type appearing in an error is necessary for admitting it
// here and not sufficient.
//
// DWORD was on that list too, and it is CORRECTED IN PLACE on the same rule and for a
// sharper reason. The sentence above said DWORD appears in no error on any file; on
// 2026-09-20 it was `src/pandoc.cpp`'s FIRST reported cause, and the reason it had been
// invisible is that pandoc.cpp used to die at <shlobj.h> before the compiler read a
// single line of it. **DWORD WAS STILL NOT ADDED.** `unsigned long` covers it
// completely, so it fails the standard-already-covers-it bar exactly as UINT did -- but
// the decisive argument is what its one appearance WAS. It was the return type of
// `DWORD WINAPI pandocWorker(LPVOID param)`, a Win32 thread callback, and the
// process-seam slice DELETED that signature: threading is standard C++ and the worker is
// an ordinary `void(PandocJob*)` now. Standing in for DWORD would have let a Win32
// callback shape keep compiling off Windows, which is the "it looked like it worked"
// failure this file's other rule exists to prevent.
//
// AND THERE IS A SECOND-ORDER COST TO ADMITTING A TYPE HERE THAT THIS HEADER HAD NOT
// WRITTEN DOWN: a stand-in makes a declaration PARSE, and a declaration that parses
// stops masking the diagnostics on its own body. Measured on pandoc.cpp -- supplying
// only its five declaration types through -include, changing nothing else, took the
// census from 12 errors on 9 lines to 19 on 16, surfacing nine Win32 names the census
// had never named. So a type admitted here can make a file's census reading GROW, and
// that growth is a better measurement rather than a regression. The reading to distrust
// is the small one.
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
// THE `WINDOWPLACEMENT` STAND-IN AND ITS ASSERTION ARE GONE AS OF 2026-09-21, and the reason is
// worth a sentence because a deleted stand-in looks like a regression in portability. It had ONE
// off-Windows reader: `App::zenRestorePlacement`, which is now `tinta::ZenRestore` -- two portable
// fields in include/window_placement.h. The SDK struct still appears in src/main_d2d.cpp, which is
// Windows-only and gets the real one from <windows.h>, so nothing off Windows names the type at
// all. A stand-in kept for zero readers is this header's version of an ungated figure: it compiles
// forever and asserts nothing about the tree.
//
// Its comment is also why it is mentioned rather than silently dropped. It read "because App stores
// one by value and zen mode round-trips it through settings.ini" -- and the second clause was
// FALSE. Nothing ever put that member in settings.ini; what round-trips is a separate local
// `WINDOWPLACEMENT` in main_d2d.cpp, decomposed into `windowMaximized` plus a position. Read as
// true, it priced the port as a settings FILE-FORMAT change and made this slice look expensive.
static_assert(WM_APP == 0x8000, "off-Windows WM_APP stands in as 0x8000");
static_assert(WM_CLOSE == 0x0010, "off-Windows WM_CLOSE stands in as 0x0010");
static_assert(WM_SETTINGCHANGE == 0x001A,
              "off-Windows WM_SETTINGCHANGE stands in as 0x001A; this SDK disagrees");

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

// The real value, so the WM_APP_* ids app.h derives from it keep the numbers they
// have on Windows. They are only ever compared against each other, but matching
// the SDK keeps a debugger session on either platform reading the same.
#define WM_APP 0x8000

// WM_CLOSE is the one SDK message id the tree names as a LITERAL rather than
// deriving, and it arrives here for the same reason WM_APP did: four sites post it
// -- editor.cpp, input.cpp and tabs.cpp x2 -- all four through
// win32_api.h::postWindowMessage, whose off-Windows arm returns false. So the
// constant has to COMPILE off Windows even though nothing will ever deliver it.
//
// That is the whole of its off-Windows meaning, and it is worth being blunt about:
// this number does not close a window here. It is an argument to a call that
// honestly fails, so the value only has to be the SDK's for a debugger and a log
// line to read the same on both platforms -- which is exactly the WM_APP argument
// above, and why the Windows branch static_asserts it against the real SDK rather
// than trusting this line.
#define WM_CLOSE 0x0010

// WM_SETTINGCHANGE arrives for the third time on the same argument, and the two sites are the same
// shape as WM_CLOSE's four: src/input.cpp posts it to itself after changing which theme index the
// system-theme follower should snap to, both times through win32_api.h::postWindowMessage, whose
// off-Windows arm returns false. So this number never delivers anything here either.
//
// The value is 0x001A and the Windows branch above static_asserts it against the real SDK rather
// than trusting this line -- which is the only reason a literal message id is allowed in this file.
#define WM_SETTINGCHANGE 0x001A

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

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

// The static_asserts that pinned each off-Windows stand-in to the real SDK type stood here.
// With no stand-ins left (see the other branch) there is nothing to pin, so they went with
// them on 2026-10-14.

#else  // !_WIN32

// NO STAND-INS LEFT. Portable code names no Win32 type since 2026-10-14: the window handle
// went first, the posted messages became tinta::PostedMessage, and key and character
// parameters take the types their callers pass. A Win32 type in portable code is now a
// compile error off Windows rather than a stand-in, and the Windows arms take the real ones
// from windows.h above.

#endif  // _WIN32

#endif  // TINTA_WIN32_TYPES_H

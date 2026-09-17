#ifndef TINTA_CLIPBOARD_H
#define TINTA_CLIPBOARD_H

#include <string>

// THE SYSTEM CLIPBOARD'S TEXT HALF, AT A SEAM WITH TWO REAL IMPLEMENTATIONS.
//
// Not in win32_api.h, by that header's own rule: it is for calls with no
// non-Windows meaning, where "off Windows these are no-ops or honest failures".
// OpenClipboard/GetClipboardData/SetClipboardData have a genuine counterpart --
// QClipboard -- so this follows platform_paths.h, keyboard.h and file_dialog.h
// instead: a declaration here, one .cpp per platform, CMake picking the file.
//
// WHAT THE CENSUS FOUND, AND WHY IT SHAPED THE SIGNATURE
//
// Seven sites, measured 2026-09-17 by SHAPE rather than by name, because a grep for
// GlobalLock finds eight and one of them is not a clipboard call at all
// (export_docx.cpp:233 locks unrelated global memory). Counting the grep would have
// over-reported the work by one and put a non-clipboard site in a clipboard seam.
//
//   site                             kind         on failure      line endings
//   annotations.cpp:361              read text    empty string    CRLF -> LF only
//   editor.cpp:572                   read text    empty string    CR -> LF, eat LF
//   input.cpp:351                    read text    empty string    CR/LF/TAB -> space
//   editor.cpp:558                   write text   silent void     --
//   utils.cpp:160                    write text   returns bool    --
//   export_docx.cpp:1464             write IMAGE  --              --
//   export_docx.cpp:1548             read IMAGE   --              --
//
// THE IMAGE HALF IS DELIBERATELY NOT HERE, and that is a scope decision with a
// reason rather than an omission. Those two sites trade in CF_DIB, a registered
// L"PNG" format and an HBITMAP; none of the three has a QClipboard shape that is a
// translation rather than a rewrite. They also live in the one file that cannot
// compile off Windows for an unrelated reason (objbase.h), so pulling them in would
// widen this seam without unblocking anything. When the image half arrives it wants
// its own census, because "put a picture on the clipboard" is a different question
// from "put text on the clipboard" in a way that "open" and "save" are not.
//
// THE THREE READS DISAGREE, AND THE DISAGREEMENT IS CARRIED RATHER THAN UNIFIED.
// This is file_dialog.h's rule about OFN_NOCHANGEDIR applied to a subtler case: a
// seam preserves behaviour, it does not improve it. All three reads strip carriage
// returns and no two do it the same way --
//
//   annotations.cpp:361  replaces the two-character sequence CRLF with LF. A LONE CR
//                        SURVIVES, and reaches the note buffer as a carriage return.
//   editor.cpp:572       maps every CR to LF and swallows an immediately following
//                        LF. A lone CR becomes a newline. Strictly more thorough.
//   input.cpp:351        maps CR, LF and TAB each to one space. Not a normalization
//                        at all -- a flattening, for a single-line input field.
//
// So the seam returns the text RAW and the three transformations are the inline
// functions below, one per existing behaviour, applied by the caller that already
// had it. Normalizing inside the seam was the first design and it is wrong in a way
// worth recording: it makes input.cpp's flatten turn CRLF into ONE space where today
// it produces TWO, because the flatten counts characters and a normalizing seam
// would have removed one before it counted. That is a visible change to what a user
// sees pasted into a filename field, arriving as a side effect of a portability
// commit, at the one site whose transformation is not a normalization.
//
// They are `inline` here rather than in a source file, as file_dialog.h's filter
// encoders and keyboard.h's modifierHeld are, and the consequence is the point: a
// test asserts all three while linking NEITHER backend, so the assertions cannot be
// satisfied by whichever platform half happens to be selected, and a stub cannot
// pass them. The transformations are also the only part with decisions in it -- the
// two functions below cannot be asserted to say much beyond a round trip.
//
// THE HWND IS GONE, and this is the one place the seam is not a straight move.
// All five text sites take an HWND and pass it to OpenClipboard. It is not part of
// the question they are asking: "what text is on the clipboard" has no window in it,
// and QClipboard is application-global with nowhere to put one. So the seam takes no
// handle and the Windows backend calls OpenClipboard(nullptr), which is documented
// and legal -- it associates the clipboard with the calling TASK instead of a window.
//
// THAT IS SAFE HERE FOR A REASON THAT COULD STOP BEING TRUE, so it is written down
// rather than left to be rediscovered. A null clipboard owner means no window
// receives WM_RENDERFORMAT, so DELAYED RENDERING cannot work. Nothing in the tree
// uses it: both write sites call SetClipboardData with a real HGLOBAL, never with
// nullptr to promise the data later. If a future site ever passes nullptr as the
// data handle, this seam is where that breaks, and it will break by producing an
// empty paste in another application rather than by failing here.
//
// ONE WRITE SITE LEAKS AND THE OTHER DOES NOT, so collapsing them fixes a bug by
// construction, which is worth naming because it contradicts the preserve-behaviour
// rule above and the contradiction is deliberate. utils.cpp:160 frees the HGLOBAL
// when SetClipboardData fails, with a comment noting ownership transfers only on
// success. editor.cpp:558 does not, so a failed SetClipboardData there orphans the
// allocation for the process's lifetime. Both become the one implementation in
// src/clipboard_win32.cpp, which does what utils.cpp did. The seam cannot preserve
// both behaviours because they differ only in whether a bug is present.
namespace platform {

// The clipboard's Unicode text, exactly as the platform reports it, or an empty
// string when there is none, when it holds no text, or when the read fails.
//
// EMPTY-FOR-ANYTHING-WRONG is not a state invented for this seam: it is what all
// three read sites already do. OpenClipboard failing, GetClipboardData returning
// null and GlobalLock returning null are three different conditions and every one
// of them reaches the caller today as an empty string, which each caller then
// treats as "nothing to paste". Returning a std::optional would be a more precise
// API and would give three callers a new branch none of them has a behaviour for.
//
// "Exactly as the platform reports it" was written as a weaker promise off Windows
// than on it, on the theory that QClipboard might normalize line endings before this
// function ever saw them. MEASURED 2026-09-17, AND IT DOES NOT: tests/clipboard_tests
// writes `first\r\nsecond` and reads back all 13 characters on BOTH platforms, so the
// CRLF survives Windows' CF_UNICODETEXT and Qt alike and the two agree.
//
// THE SCOPE OF THAT READING IS NARROWER THAN THE CLAIM IT REPLACES, so it is stated
// rather than rounded up. The Qt half was measured under the OFFSCREEN QPA plugin,
// whose clipboard is in-process; it is not evidence about xcb, wayland or the Windows
// QPA plugin, each of which goes through a real system clipboard that could convert.
// So the seam still does not PROMISE the round trip -- the test reports it as a
// measurement rather than asserting it, and the reason is this paragraph.
//
// If a platform ever does normalize, the only observable difference is at input.cpp's
// flatten, per the header note: the other two transformations are idempotent over
// already-normalized text, which tests/clipboard_tests.cpp asserts directly.
std::wstring clipboardText();

// Replace the clipboard's contents with `text`. Returns whether it worked.
//
// Empty text returns false WITHOUT touching the clipboard, which is utils.cpp's
// contract and is already asserted at tests/file_path_tests.cpp:207. Getting this
// wrong the other way -- emptying the clipboard on an empty request -- would destroy
// whatever the user had copied, from a code path that thought it was doing nothing.
bool setClipboardText(const std::wstring& text);

// --- The three transformations, split out so they can be asserted --------------

// annotations.cpp:361's rule: the two-character sequence CRLF becomes LF, and a
// lone CR is left alone. Kept as its own function rather than folded into
// normalizeNewlines below precisely BECAUSE it is the weaker of the two: the two
// sites disagree about a lone CR, and a seam that quietly picked one would change
// the other. Which of the two is better is a real question and not this commit's.
inline std::wstring normalizeCrLfOnly(std::wstring text) {
    std::size_t pos = 0;
    while ((pos = text.find(L"\r\n", pos)) != std::wstring::npos) {
        text.replace(pos, 2, L"\n");
    }
    return text;
}

// editor.cpp:572's rule: every CR becomes LF, and an LF immediately after a CR is
// swallowed. So CRLF -> LF, a lone CR -> LF, and a lone LF is untouched.
inline std::wstring normalizeNewlines(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') {
            out += L'\n';
            if (i + 1 < text.size() && text[i + 1] == L'\n') i++;
        } else {
            out += text[i];
        }
    }
    return out;
}

// input.cpp:351's rule, used by input.cpp and editor.cpp:2252: CR, LF and TAB each
// become ONE space. Character-for-character, so CRLF becomes two spaces -- see the
// header note on why that is preserved rather than tidied.
inline std::wstring flattenToSingleLine(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return text;
}

}  // namespace platform

#endif  // TINTA_CLIPBOARD_H

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
// GlobalLock found eight and one of them was not a clipboard call at all
// (export_docx.cpp locked unrelated global memory). Counting the grep would have
// over-reported the work by one and put a non-clipboard site in a clipboard seam.
//
// THAT GREP NO LONGER REPRODUCES, and the sentence is kept in the past tense rather than
// refreshed, because the reason it was worth writing survives its own numbers. As of
// 2026-09-20 `grep -rn GlobalLock src/` returns 7 across clipboard_win32.cpp,
// image_win32.cpp and print.cpp, and NONE in export_docx.cpp -- the site it warned about
// is gone along with the rest of that file's Win32 surface. A name census being wrong by
// one is the point; which one it was wrong about is not.
//
// EVERY ADDRESS IN THE TABLE BELOW IS HISTORY, AS OF 2026-09-20, and is deliberately
// not refreshed. The table is the PRE-ROUTING census: those seven sites no longer hold
// the calls it describes -- they hold the seam calls that replaced them -- so a fresher
// number would not point at anything the table claims. Read the first column as "the
// file that used to do this", and grep the seam's own name to find where it does it now.
// Distinguish this from a rotted LIVE citation, which is a defect and is fixed by
// naming the function; a dated census is a reading, and a reading is allowed to age.
//
//   site                             kind         on failure      line endings
//   annotations.cpp:361              read text    empty string    CRLF -> LF only
//   editor.cpp:572                   read text    empty string    CR -> LF, eat LF
//   input.cpp handleKeyDown          read text    empty string    CR/LF/TAB -> space
//   editor.cpp:558                   write text   silent void     --
//   utils.cpp:160                    write text   returns bool    --
//   export_docx.cpp:1464             write IMAGE  --              --
//   export_docx.cpp:1548             read IMAGE   --              --
//
// THE IMAGE HALF IS HERE NOW, 2026-09-19, AND THE SENTENCE THAT KEPT IT OUT ASKED FOR
// EXACTLY THE PARAGRAPH BELOW. It read: "THE IMAGE HALF IS DELIBERATELY NOT HERE, and
// that is a scope decision with a reason rather than an omission. Those two sites trade
// in CF_DIB, a registered L\"PNG\" format and an HBITMAP; none of the three has a
// QClipboard shape that is a translation rather than a rewrite. They also live in the one
// file that cannot compile off Windows for an unrelated reason (objbase.h), so pulling
// them in would widen this seam without unblocking anything. When the image half arrives
// it wants its own census, because 'put a picture on the clipboard' is a different
// question from 'put text on the clipboard' in a way that 'open' and 'save' are not."
//
// THE CENSUS IT ASKED FOR, and the reason the refusal expired rather than being
// overruled. Three claims were made and they do not all survive:
//
//   1. "none of the three has a QClipboard shape that is a translation" -- HALF FALSE.
//      CF_DIB -> QClipboard::setImage, the registered PNG -> QMimeData::setData with
//      "image/png", and QMimeData::imageData -> QImage are each a translation. The
//      Windows CF_BITMAP READ is the one genuine rewrite, and it is the rewrite that
//      pays for the slice: WIC out, GDI GetDIBits in, which is how IWICBitmap left
//      src/export_docx.cpp.
//   2. "would widen this seam without unblocking anything" -- FALSE, AND IT WAS THE CLAIM
//      THIS FILE WAS MOST CONFIDENT ABOUT. It was written here as "SURVIVES AS WRITTEN
//      and is still true. src/export_docx.cpp dies on objbase.h before and after. This
//      seam did not unblock that file and does not claim to; what it unblocked is WIC's
//      presence in that file's clipboard path." Measured 2026-09-20: this seam unblocked
//      src/export_docx.cpp COMPLETELY. Taking the CF_BITMAP read removed that file's last
//      COM caller, which left <objbase.h>, <ole2.h> and <wincodec.h> as dead includes --
//      so it did not "die on objbase.h before and after", it died on a header it no longer
//      needed, for one day, while three comments explained why that was expected.
//      The file is on tests/portable-sources.txt and censuses exit 0, fatal 0, errors 0.
//
//      WHY THE WRONG ANSWER WAS THE CONFIDENT ONE IS THE REUSABLE PART. The claim was
//      checked against the census, the census agreed, and the census agreed because it
//      stops at the first unresolvable header -- so the instrument's answer was a fact
//      about the include line and was read as a fact about the file. A seam cannot know
//      it was the last caller of a header; only recompiling without the header can, and
//      nothing in this tree did that until somebody asked why the row was fatal.
//   3. The third claim is at clipboardImage() below, because it is about the DATA and
//      not about the census: "moving it would drag ID2D1/WIC types through a header that
//      has none." That premise is the one that expired, and it expired because
//      tinta::DecodedImage arrived.
//
// So a refusal made of three reasons lost one, kept one, and had the third expire under
// it -- which is why each is answered separately instead of the block being deleted. A
// premise going stale is not a decision reversing itself; src/settings_app.cpp is the
// counter-example in this tree, where the stated premise went false and the exclusion
// still held on re-argument.
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
//   input.cpp            maps CR, LF and TAB each to one space. Not a normalization
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
// allocation for the process's lifetime. Both became the one implementation in
// src/clipboard_win32.cpp, which did what utils.cpp did, until that file was deleted on
// 2026-10-07 for src/clipboard_qt.cpp. The seam could not preserve both behaviours
// because they differed only in whether a bug was present.

// FORWARD-DECLARED RATHER THAN #include "image_api.h", and the reason is the same one
// image_api.h itself gives for forward-declaring RenderTarget: a declaration costs
// nothing and carries no condition. image_api.h pulls in geometry.h, which pulls
// <d2d1.h> inside #ifdef _WIN32 -- harmless, but it would mean this header transitively
// named Direct2D types on Windows, and "a header that has none" is the exact phrase the
// image half was kept out of this seam with. Keeping it literally true is cheaper than
// explaining that it is only nearly true. A reference parameter and a by-value return
// type in a DECLARATION need no definition; every consumer includes image_api.h anyway,
// because it cannot do anything with a DecodedImage without it.
namespace tinta {
struct DecodedImage;
}

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

// Does the clipboard currently hold an IMAGE? Not the image itself -- just the
// availability question.
//
// THE NARROWNESS WAS THE POINT AND IT IS NOT ANY MORE, which is a state change rather
// than a correction, so the retired paragraph is kept whole: "THE NARROWNESS IS THE POINT,
// because this header's census deliberately left the image half of the clipboard outside
// the seam and src/clipboard_win32.cpp says so. This admits exactly one bit of it and no
// more: IsClipboardFormatAvailable(CF_BITMAP) with nothing read. The DATA half -- CF_DIB,
// the registered L\"PNG\" format, the CF_BITMAP handle read, and the WIC decode they feed
// -- stays in export_docx.cpp, because moving it would drag ID2D1/WIC types through a
// header that has none." The data half is two declarations below as of 2026-09-19, and
// this function is no longer the narrow admission of a larger thing left out. It is just
// the cheap question, kept separate because it answers without opening the clipboard.
//
// STILL TWO SITES, BUT NOT THE SAME TWO, and the difference matters to anyone auditing
// the redundancy. It used to be editor.cpp:1649 and export_docx.cpp:1545 -- the second
// being the first line of clipboardImageToPngFile, the function the first goes on to
// call. Now it is editor.cpp and clipboardImage() below, which asks it as its own first
// line, so the redundancy survived the move intact and moved INTO this seam.
//
// The editor's check stays REDUNDANT with its callee's, and is still routed rather than
// deleted: dropping it would leave editor.cpp running up to 100 fs::exists calls hunting
// for a free `-img-NN.png` filename before the callee refused, on every plain-text paste.
// Same outcome, different work, so removing it is a behaviour change and not a
// simplification.
//
// OFF WINDOWS THIS IS REAL, NOT A NO-OP, and it is the first thing in this port's
// Windows-facing surface that can say that: QClipboard's mime data answers hasImage()
// directly. So unlike the cursor and capture inlines, there is nothing owed here
// later. What the caller then does with a true answer off Windows is a separate
// question and currently ends at clipboardImageToPngFile, which is Windows-only --
// which is a LINK fact about that file, not a gap in this one.
//
// False on any failure, matching clipboardText's empty-for-anything-wrong: the
// caller's existing else-branch is an ordinary text paste, which is exactly right.
//
// THE "TWO SITES" AND "NOTHING OWED HERE LATER" ABOVE ARE STILL TRUE, and the clause
// that has gone is the last one: "What the caller then does with a true answer off
// Windows is a separate question and currently ends at clipboardImageToPngFile, which is
// Windows-only." It no longer ends there -- that function's body is the two calls below
// plus tinta::encodePng, all three of which exist on both platforms.
bool clipboardHasImage();

// --- The image DATA half, which this header refused until 2026-09-19 ---------------

// Put `image` on the clipboard, replacing its contents. False on any failure.
//
// `pngBytes` is the SAME image already encoded as PNG -- tinta::encodePng(image) -- and
// it is a parameter rather than something this seam computes, which is a link decision
// and not a style one. tinta::encodePng lives in src/image_win32.cpp and src/image_qt.cpp;
// calling it from here would put the whole WIC and Direct2D link line onto
// tests/clipboard_tests, a target whose entire value is that it links ONE platform file
// and nothing else. The caller already has the bytes at both existing call sites.
//
// BOTH ARMS USE BOTH ARGUMENTS, which is why this is not a Windows parameter that Qt
// ignores. Windows publishes CF_DIB plus the registered L"PNG" format, because
// applications differ in which they prefer and the original code offered both. Qt
// publishes image/png on the same QMimeData as the image itself, for the same reason.
// A parameter one backend discarded would be a parameter that only documents an
// intention -- see image_api.h's note on why encodePng takes no format enum.
//
// An empty `pngBytes` is legal and means "bitmap only": the image still lands and this
// still returns true. That is the honest reading of the original code, which treated a
// failed PNG encode as a reason to publish less rather than to publish nothing.
//
// THE CLIPBOARD IS OPAQUE AND THAT IS A CONTRACT, NOT A LIMITATION DISCOVERED LATE.
// CF_DIB's alpha byte is ignored by the format and CF_BITMAP carries no alpha at all,
// so a partially transparent DecodedImage cannot survive this round trip on Windows and
// nothing here pretends otherwise. Callers in this tree rasterise over an opaque theme
// background, so every pixel they hand over already has alpha 255.
bool setClipboardImage(const tinta::DecodedImage& image, const std::string& pngBytes);

// The clipboard's image as premultiplied BGRA, or a DecodedImage with ok == false when
// there is none, when the read fails, or when the dimensions are outside
// tinta::kMaxImageDimension.
//
// THE PREMISE THAT KEPT THIS OUT HAS EXPIRED, and it is worth separating from the census
// at the top of this file because it is a different claim. It read: "The DATA half --
// CF_DIB, the registered L\"PNG\" format, the CF_BITMAP handle read, and the WIC decode
// they feed -- stays in export_docx.cpp, because moving it would drag ID2D1/WIC types
// through a header that has none." That was true of the SHAPE the data had at the time,
// an IWICBitmap. tinta::DecodedImage (include/image_api.h) is premultiplied BGRA bytes in
// a std::vector<uint8_t> and names no COM type, so the data half expressed in it drags
// nothing -- this header still includes only <string> and still forward-declares rather
// than including image_api.h, so not even a transitive <d2d1.h> reaches a consumer.
//
// SO THE WIC DECODE DID NOT MOVE HERE; IT WAS DELETED. src/export_docx.cpp's
// clipboardBitmapToPng -- CreateBitmapFromHBITMAP, an IWICBitmapEncoder and
// GetHGlobalFromStream, 52 lines -- is gone, and src/clipboard_win32.cpp read the
// bitmap with GDI's GetDIBits instead (until 2026-10-07, when QClipboard replaced it). export_docx.cpp predicted exactly this and called
// it "a different slice against a recorded decision", which is what this is. (The line
// number that used to sit in this sentence is dropped rather than refreshed -- it had
// already rotted once, and the quoted phrase locates the comment under grep without
// depending on where it sits in a file that keeps growing.)
//
// ALPHA IS FORCED TO 255 AND THE REASON IS A SILENT FAILURE THE OLD CODE NAMED. A
// CF_BITMAP has no alpha channel, so the bytes GetDIBits returns have an alpha byte that
// means nothing -- and MEASURED 2026-09-19, a 32bpp BI_RGB DIB published with zero there
// reads back with all of it still zero, so the byte passes through GDI untouched. (An
// earlier draft of this paragraph said it "is frequently ZERO" in practice, which was a
// claim about other applications with nothing behind it. How often a real producer leaves
// zero there is unmeasured and does not need measuring: undefined is the contract and once
// is enough.) Handing those straight back
// under this function's premultiplied-BGRA contract would produce, in export_docx.cpp's
// own words about the WIC version of the same trap, "a fully transparent PNG -- a
// plausible-looking file with nothing in it". Every length check and every `ok` flag
// passes on that file. So the Windows implementation overwrites the alpha byte, and
// tests/clipboard_tests.cpp asserts a PIXEL rather than a size.
//
// OFF WINDOWS THE OPACITY IS ALSO ENFORCED, AND IT IS NOT A CARRIED-OVER WORKAROUND: Qt
// can hand back an image with real transparency, and this seam's contract says the
// clipboard is opaque. So the Qt half converts to QImage::Format_RGB32 -- the format
// whose own definition is 0xffRRGGBB -- and then writes the alpha byte anyway, because
// the invariant this function promises should not depend on what a conversion does
// internally. What the COLOUR bytes are after that is whatever Qt's conversion to an
// opaque format produced; this seam does not composite, and claiming a particular
// blend would be asserting a Qt internal nothing here has measured.
tinta::DecodedImage clipboardImage();

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

// input.cpp's rule -- applied in handleKeyDown's Ctrl+V branch, again in
// handleCharInput's, and in editor.cpp as well: CR, LF
// and TAB each become ONE space. Character-for-character, so CRLF becomes two spaces
// -- see the header note on why that is preserved rather than tidied.
//
// THIS ADDRESS WAS `:351` FOR SEVERAL SPANS AND POINTED AT A DELETED FUNCTION, which
// is a citation failure with a decision in it rather than an arithmetic fix. `:351`
// was `clipboardLine`'s definition; the tombstone above input.cpp's
// `closeFolderBrowserInput` records its removal and pins
// `:351` as the historical address, so a sweep can reasonably argue the number is
// FROZEN and correct as history. It is not, here: these three sites name a RULE and
// the places that apply it, both of which are live, so freezing the number would have
// left three comments pointing at `closeFolderBrowserInput` -- an unrelated function
// that moved in underneath them. The test is what the sentence PROMISES the reader
// will be at the line, not whether the line once held the thing.
//
// AND THE FRESHER NUMBER DID NOT SURVIVE EITHER. `:3538` and `:4172` were themselves
// corrections, and one slice of edits ABOVE them in a 4,400-line file put them out by
// 28. The addresses are now FUNCTION NAMES throughout this header for that reason: the
// claim is about which handler pastes into a single-line field, and a handler name is
// both what the claim means and the thing that cannot drift.
inline std::wstring flattenToSingleLine(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return text;
}

}  // namespace platform

#endif  // TINTA_CLIPBOARD_H

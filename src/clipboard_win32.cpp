// The Windows half of the clipboard seam (include/clipboard.h): the text half of
// OpenClipboard / GetClipboardData / SetClipboardData, with the five call sites'
// behaviour preserved rather than tidied -- except at the one place they contradict
// each other, which the header names.
//
// The write below is utils.cpp:160's version, not editor.cpp:558's. They differ in
// whether the HGLOBAL is freed when SetClipboardData fails, so there is no way to
// keep both: one of the two behaviours is a leak. See the header note.
//
// The reads pass nullptr to OpenClipboard where every site passed a real HWND. That
// is the seam's one genuine behaviour change and the header states its precondition
// -- nothing here uses delayed rendering, so nothing needs an owner window to
// receive WM_RENDERFORMAT.
//
// This file is NOT the only place under src/ that touches the clipboard, and saying
// so is the honest version: export_docx.cpp still holds the IMAGE half (CF_DIB, a
// registered L"PNG" format, and a CF_BITMAP read). Those are deliberately outside
// this seam, per the header's census.
//
// AMENDED 2026-09-17, because the sentence above stopped being exactly true: one bit
// of the image half is now here -- clipboardHasImage, the AVAILABILITY question and
// nothing else. The data half is unchanged and still lives in export_docx.cpp. The
// distinction is not a technicality: asking whether a format is on the clipboard needs
// no handle, no lock and no decoder, so it costs this header nothing, whereas reading
// the bitmap would drag WIC and Direct2D types through it.
//
// AMENDED AGAIN 2026-09-19, and this time the amendment is the whole data half rather
// than one bit of it. Both paragraphs above are now history and are kept as history:
// CF_DIB, the registered L"PNG" format and the CF_BITMAP read all live here, in
// setClipboardImage and clipboardImage below. The 2026-09-17 reason -- "reading the
// bitmap would drag WIC and Direct2D types through it" -- described the shape the data
// had then, an IWICBitmap. It is expressed in tinta::DecodedImage now, which is a
// std::vector<uint8_t> and a pair of dimensions, so the drag it feared is gone. What is
// left in export_docx.cpp is the DOCX half: the OPC package, the relationships, and the
// XML.
//
// AND THE SENTENCE THAT USED TO END THAT PARAGRAPH WAS FALSE WHEN THIS SLICE WROTE IT.
// It read: "That file still cannot compile off Windows, for objbase.h and not for this."
// The first clause was wrong and the second was the reason it went unchecked -- pointing
// at a DIFFERENT file's dependency is what made it feel like a measurement. On 2026-09-20
// the three Windows headers were deleted from export_docx.cpp and it compiled clean off
// Windows at the first attempt, because moving the CF_BITMAP read down here -- the very
// change this paragraph is announcing -- took away its last COM caller. The DOCX half
// never needed objbase.h at all: the zip and the WordprocessingML are written by hand
// through <fstream> and std::string.
//
// The shape is worth more than the fact. This paragraph is an AMENDMENT, written by
// somebody who had just caught the two paragraphs above it being stale, said so, and
// kept them as history -- and who then added a fresh claim about the file next door
// without running the one command that would have tested it. The same slice wrote the
// same false sentence into CMakeLists.txt (where it was flagged "RIGHT") and into
// include/clipboard.h (where it was "SURVIVES AS WRITTEN"). Three sites, three
// confidence markers, one unmeasured claim, and they cited each other. Being in the
// middle of a correction is when a neighbouring sentence is least likely to be checked.
//
// THE READ IS GDI AND THAT IS THE POINT OF THE SLICE. The old path went
// CF_BITMAP -> IWICBitmap -> IWICBitmapEncoder -> a PNG in an HGLOBAL, and it needed the
// App's imaging factory to do it. This one goes CF_BITMAP -> GetDIBits -> bytes, the PNG
// is the caller's business through tinta::encodePng. No COM, no factory, no App -- which
// is what lets a test link this file and nothing else.
//
// image_api.h IS included here and deliberately is not in the header. This is a .cpp:
// nothing includes it, so what it pulls in reaches no other translation unit. See the
// header's note above namespace tinta for why the declaration side stays a forward
// declaration.

#include "clipboard.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "image_api.h"

namespace platform {

namespace {

// OpenClipboard WITH RETRY, and the retry is there because the failure rate was MEASURED
// rather than feared. The clipboard is a single global resource that only one process may
// hold open at a time, so OpenClipboard loses a race whenever anything else is reading it
// -- and on a stock Windows 11 desktop something always is: Clipboard History and cloud
// clipboard both wake on every change.
//
// THE NUMBER, taken 2026-09-19 on this machine with 200 consecutive attempts per arm:
// setClipboardText refused 33 and then 53 times out of 200. Between a sixth and a quarter
// of clipboard writes were failing, silently, in the shipped application -- a Ctrl+C that
// did nothing, reported through a bool nobody displays. The image half measured worse
// (70-82 of 200) because it opens the clipboard for a larger payload, and the read failed
// 27-35 of 200.
//
// So this is a bug fix arriving with a port, which the header already names as this seam's
// one deliberate exception to preserve-behaviour: it made the same call for utils.cpp's
// free-on-failure over editor.cpp's leak. Five call sites could not each grow a retry
// loop; one seam can have exactly one.
//
// TEN ATTEMPTS, 10 ms APART, so the worst case is about 100 ms and the common case is one
// call. The cap matters more than the count: a clipboard genuinely held open by a stuck
// process must still return rather than hang a UI thread, and the callers all have a
// documented behaviour for failure already -- an empty string, or false.
//
// IsClipboardFormatAvailable needs none of this and the same measurement says so:
// avail_fail was 0 of 200 in both runs, because it never opens the clipboard at all.
bool openClipboardRetrying() {
    for (int attempt = 0; attempt < 10; attempt++) {
        if (OpenClipboard(nullptr)) return true;
        Sleep(10);
    }
    return false;
}

}  // namespace

std::wstring clipboardText() {
    std::wstring out;
    if (!openClipboardRetrying()) return out;
    // Three separate failures -- no text format, and a lock that does not take --
    // both leave `out` empty, which is what all three read sites already do with
    // them. See the header on why this is not a std::optional.
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = (const wchar_t*)GlobalLock(data)) {
            out = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return out;
}

bool setClipboardText(const std::wstring& text) {
    if (text.empty()) return false;

    // Allocated BEFORE the clipboard is opened, as utils.cpp did. The order matters
    // for the failure path rather than for correctness: an allocation failure here
    // returns without ever having opened the clipboard, so there is no window in
    // which this process holds it open and another cannot.
    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;
    wchar_t* dest = (wchar_t*)GlobalLock(hMem);
    if (!dest) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(dest, text.c_str(), size);
    GlobalUnlock(hMem);

    if (!openClipboardRetrying()) {
        GlobalFree(hMem);
        return false;
    }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hMem);
    if (!copied) GlobalFree(hMem);  // ownership transfers only on success
    CloseClipboard();
    return copied;
}

bool clipboardHasImage() {
    // No OpenClipboard, and that is the API rather than an omission:
    // IsClipboardFormatAvailable is explicitly documented as usable without opening
    // the clipboard, which is why it can be a one-liner. editor.cpp:1649 called it
    // exactly this way, so this is a carry with no change at all.
    return IsClipboardFormatAvailable(CF_BITMAP) != 0;
}

bool setClipboardImage(const tinta::DecodedImage& image, const std::string& pngBytes) {
    if (!image.ok || image.width == 0 || image.height == 0) return false;
    const size_t stride = (size_t)image.width * 4;
    if (image.pixels.size() != stride * image.height) return false;

    // THE SIZE CHECK IS NEW AND IT IS NOT DEFENSIVE PADDING. DecodedImage documents
    // that relation as its invariant when ok is true, but this function is about to
    // memcpy `stride` bytes out of the buffer `height` times, so an ok flag arriving
    // from somewhere that got the invariant wrong would be a heap overread rather than
    // a bad picture. tinta::encodePng makes exactly the same check for the same reason
    // (src/image_win32.cpp), so this matches how the tree already treats that struct
    // at a boundary where the cost of trusting it is memory safety.

    // CF_DIB: header plus BOTTOM-UP 32bpp rows, which is why the loop below reads the
    // source backwards. DecodedImage is top-down; a positive biHeight means bottom-up.
    // The alternative -- a negative biHeight and a straight memcpy -- is legal in the
    // BITMAPINFOHEADER spec but is not what the clipboard's consumers expect, and the
    // original code in export_docx.cpp's CF_DIB writer built it this way. (Addressless on
    // purpose: that code is HERE now, so a line number in that file points at whatever
    // happens to occupy the line today -- and on 2026-09-20 an edit that changed no code
    // there at all moved every line past 32 by 22.)
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + image.pixels.size());
    if (!dib) return false;
    if (BYTE* out = (BYTE*)GlobalLock(dib)) {
        BITMAPINFOHEADER hdr{};
        hdr.biSize = sizeof(hdr);
        hdr.biWidth = (LONG)image.width;
        hdr.biHeight = (LONG)image.height;
        hdr.biPlanes = 1;
        hdr.biBitCount = 32;
        hdr.biCompression = BI_RGB;
        hdr.biSizeImage = (DWORD)image.pixels.size();
        memcpy(out, &hdr, sizeof(hdr));
        for (unsigned row = 0; row < image.height; row++) {
            memcpy(out + sizeof(hdr) + (size_t)row * stride,
                   image.pixels.data() + (size_t)(image.height - 1 - row) * stride, stride);
        }
        GlobalUnlock(dib);
    } else {
        GlobalFree(dib);
        return false;
    }

    // The registered "PNG" format, published alongside rather than instead. Empty
    // pngBytes is legal and means bitmap-only -- see the header. A failed allocation
    // here is treated the same way: publish less, not nothing.
    HGLOBAL pngGlobal = nullptr;
    if (!pngBytes.empty()) {
        pngGlobal = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
        if (pngGlobal) {
            if (void* bits = GlobalLock(pngGlobal)) {
                memcpy(bits, pngBytes.data(), pngBytes.size());
                GlobalUnlock(pngGlobal);
            } else {
                GlobalFree(pngGlobal);
                pngGlobal = nullptr;
            }
        }
    }

    if (!openClipboardRetrying()) {
        GlobalFree(dib);
        if (pngGlobal) GlobalFree(pngGlobal);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
    if (!ok) GlobalFree(dib);  // ownership transfers only on success, as in setClipboardText
    if (pngGlobal) {
        UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        if (!pngFormat || !SetClipboardData(pngFormat, pngGlobal)) {
            GlobalFree(pngGlobal);
        }
    }
    CloseClipboard();
    return ok;
}

tinta::DecodedImage clipboardImage() {
    tinta::DecodedImage image;
    // The availability question first, through this file's own answer to it rather than
    // a second IsClipboardFormatAvailable call -- export_docx.cpp's image-paste path used
    // to ask it raw there, which is what put that site on the census in the first place.
    if (!clipboardHasImage()) return image;
    if (!openClipboardRetrying()) return image;

    if (HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP)) {
        BITMAP info{};
        // GetObject returns the byte count it wrote, so a short write is a failure and
        // not a partially usable struct. bmWidth/bmHeight are always positive for a
        // DDB; the bound is tinta::kMaxImageDimension's, EXCLUSIVE, because the
        // allocation below happens before a single pixel has been read and the seam
        // enforces that bound identically in both backends.
        if (GetObject(hbm, sizeof(info), &info) == sizeof(info) && info.bmWidth > 0 &&
            info.bmHeight > 0 && (unsigned)info.bmWidth < tinta::kMaxImageDimension &&
            (unsigned)info.bmHeight < tinta::kMaxImageDimension) {
            const unsigned width = (unsigned)info.bmWidth;
            const unsigned height = (unsigned)info.bmHeight;
            std::vector<uint8_t> pixels((size_t)width * height * 4);

            // A NEGATIVE biHeight HERE, which is the opposite sign to the write above,
            // and the asymmetry is correct rather than an oversight: CF_DIB's consumers
            // want bottom-up, and DecodedImage wants top-down. Asking GetDIBits for
            // top-down rows costs nothing and removes a flip loop that could only be
            // wrong in one direction.
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = (LONG)width;
            bi.bmiHeader.biHeight = -(LONG)height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            if (HDC dc = GetDC(nullptr)) {
                int rows =
                    GetDIBits(dc, hbm, 0, height, pixels.data(), &bi, DIB_RGB_COLORS);
                ReleaseDC(nullptr, dc);
                if (rows == (int)height) {
                    // ALPHA IS FORCED, AND THIS LINE IS THE WHOLE REASON THE READ IS
                    // SAFE TO EXPRESS AS A DecodedImage. A CF_BITMAP carries no alpha
                    // channel; BI_RGB at 32bpp leaves the fourth byte undefined.
                    //
                    // MEASURED, because an earlier draft of this comment asserted the
                    // byte "is frequently zero" in practice and that was a claim about
                    // other applications with nothing behind it. What IS measured
                    // (2026-09-19, tests/clipboard_tests.cpp's publishZeroAlphaDib): a
                    // 32bpp BI_RGB DIB published with zero in every fourth byte reads
                    // back through GetDIBits with all 12 of 12 still zero, so the byte
                    // passes through GDI untouched and this line is the only thing
                    // between it and a DecodedImage. How OFTEN a real producer leaves
                    // zero there is still not measured and does not need to be -- once
                    // is enough, and undefined is the contract.
                    //
                    // Premultiplied BGRA with alpha 0
                    // means a fully transparent pixel, so handing these bytes back
                    // untouched would satisfy every length check and every ok flag and
                    // still encode, in src/export_docx.cpp's own words about the WIC
                    // version of this trap, "a plausible-looking file with nothing in
                    // it". With alpha at 255 the premultiplied and straight
                    // representations of a pixel are identical, so there is no
                    // multiplication to do and no colour to correct.
                    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
                    image.width = width;
                    image.height = height;
                    image.pixels = std::move(pixels);
                    image.ok = true;
                }
            }
        }
    }
    CloseClipboard();
    return image;
}

}  // namespace platform

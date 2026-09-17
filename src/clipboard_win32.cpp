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

#include "clipboard.h"

#include <windows.h>

#include <cstring>

namespace platform {

std::wstring clipboardText() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
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

    if (!OpenClipboard(nullptr)) {
        GlobalFree(hMem);
        return false;
    }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hMem);
    if (!copied) GlobalFree(hMem);  // ownership transfers only on success
    CloseClipboard();
    return copied;
}

}  // namespace platform

#include "tab_ghost_win32.h"

#ifdef _WIN32

#include <cstdint>
#include <cstring>

namespace tinta {

const wchar_t* const kTabGhostWindowClass = L"TintaTabGhost";

namespace {

COLORREF toColorref(const Color& c) {
    return RGB((BYTE)(c.r * 255.0f + 0.5f), (BYTE)(c.g * 255.0f + 0.5f),
               (BYTE)(c.b * 255.0f + 0.5f));
}

// Registered once per process, on first use. `static bool` rather than a RegisterClassExW whose
// second call is allowed to fail: the class outlives every card, and re-registering is an error the
// original code was already avoiding this way.
void ensureClassRegistered() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTabGhostWindowClass;
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND tabGhostCreate(const TabGhostCard& card, const std::wstring& title, int screenX, int screenY) {
    ensureClassRegistered();

    const TabGhostGeometry& g = card.geometry;

    HWND ghost = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kTabGhostWindowClass, L"", WS_POPUP, screenX, screenY, g.width, g.height, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!ghost) return nullptr;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = g.width;
    info.bmiHeader.biHeight = -g.height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ oldBitmap = SelectObject(memory, dib);
        std::memset(bits, 0, (size_t)g.width * g.height * 4);

        // Card: strip-colored fill, subtle border. The +1 and the doubled radius are GDI's
        // convention for CreateRoundRectRgn, not a second opinion about the card's shape --
        // tabGhostCovers is the authority on which pixels belong to it, and the alpha rebuild below
        // is where that authority is applied.
        HRGN region = CreateRoundRectRgn(0, 0, g.width + 1, g.height + 1, (int)(g.radius * 2),
                                         (int)(g.radius * 2));
        HBRUSH fill = CreateSolidBrush(toColorref(card.fill));
        FillRgn(memory, region, fill);
        HBRUSH border = CreateSolidBrush(toColorref(card.border));
        FrameRgn(memory, region, border, 1, 1);
        DeleteObject(fill);
        DeleteObject(border);
        DeleteObject(region);

        // Title
        HFONT font = CreateFontW(-(int)g.fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(memory, font);
        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, toColorref(card.text));
        RECT textRect = {g.textLeftInset, 0, g.width - g.textRightInset, g.height};
        DrawTextW(memory, title.c_str(), (int)title.size(), &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(memory, oldFont);
        DeleteObject(font);

        // Rebuild alpha: opaque inside the rounded card, clear outside. GDI text writes alpha 0, so
        // without this pass the glyphs would be the only transparent part of the card.
        //
        // The membership test is tabGhostCovers, called rather than repeated. It used to be four
        // branches of coordinate arithmetic inline here, which is the one shape in this function
        // that a Qt arm would have had to reproduce from scratch.
        uint32_t* pixels = static_cast<uint32_t*>(bits);
        for (int py = 0; py < g.height; py++) {
            for (int px = 0; px < g.width; px++) {
                uint32_t& p = pixels[(size_t)py * g.width + px];
                p = tabGhostCovers(g, px, py) ? (p | 0xFF000000u) : 0u;
            }
        }

        POINT source = {0, 0};
        POINT position = {screenX, screenY};
        SIZE size = {g.width, g.height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, kTabGhostAlpha, AC_SRC_ALPHA};
        UpdateLayeredWindow(ghost, screen, &position, &size, memory, &source, 0, &blend, ULW_ALPHA);
        SelectObject(memory, oldBitmap);
        DeleteObject(dib);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    ShowWindow(ghost, SW_SHOWNOACTIVATE);
    return ghost;
}

void tabGhostMove(HWND ghost, int screenX, int screenY) {
    if (!ghost) return;
    SetWindowPos(ghost, HWND_TOPMOST, screenX, screenY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

void tabGhostDestroy(HWND& ghost) {
    if (!ghost) return;
    DestroyWindow(ghost);
    ghost = nullptr;
}

}  // namespace tinta

#endif  // _WIN32

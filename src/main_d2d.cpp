// Direct2D + DirectWrite renderer for Windows
// Much faster startup than OpenGL

#include "app.h"
#include "platform_paths.h"

#include <windowsx.h>
#include <shellapi.h>
// <shlobj.h> was here and had no caller: swept 2026-09-20 by the screen that
// export_docx.cpp's three dead includes prompted. shellapi.h above it is live
// (6 sites), which is the point -- a dead include sits beside a live one and
// nothing distinguishes them but a grep for the symbols.

#include <d3d11.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include "settings.h"
#include "selection.h"
#include "document.h"
#include "d2d_init.h"
// The chrome falsifier's one entry point, behind --dumpchrome below.
#include "chrome_dump.h"
#include "text_formats.h"
// The placement decision, which used to be thirty-five lines of this file.
#include "window_placement.h"
// The option chain, which used to be thirty-six more.
#include "launch_options.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "render.h"
#include "paint_document.h"
#include "render_frame.h"
#include "scrollbar.h"
#include "file_utils.h"
#include "overlays.h"
#include "annotations.h"
#include "drafts.h"
#include "signals.h"
#include "tableedit.h"
#include "editrail.h"
#include "startpage.h"

#include <appmodel.h>
#include <winhttp.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")
#include "input.h"
#include "editor.h"
#include "print.h"
#include "export.h"
#include "i18n.h"
#include "tabs.h"
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static App* g_app = nullptr;

// Initializes the GPU driver on a worker thread after the first (software)
// frame is on screen. D3D device creation costs ~200 ms dominated by
// process-global driver initialization — done here once, the hardware
// render target created on WM_APP_GPU_READY takes ~40 ms. The warm-up
// device is held only until that target exists (its own device keeps the
// driver initialized), then released — it pins tens of MB otherwise.
static ID3D11Device* g_warmupDevice = nullptr;
static void startGpuWarmup() {
    std::thread([] {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL fl;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                          D3D11_SDK_VERSION, &device, &fl, &ctx);
        if (ctx) ctx->Release();
        if (device && g_app && g_app->hwnd) {
            g_warmupDevice = device;
            postWindowMessage(g_app->hwnd, WM_APP_GPU_READY, 0, 0);
        } else if (device) {
            device->Release();
        }
    }).detach();
}

// Windows light/dark preference (Settings > Personalization > Colors)
static bool systemPrefersLight() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return true;
    }
    return value != 0;
}

// The theme the auto mode wants right now -- the choice is theme_apply.h's, shared with qinta
static int autoThemeIndex(const App& app) {
    return followThemeIndex(app, systemPrefersLight());
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);

void render(App& app) {
    if (!app.renderTarget) return;

    // THE FRAME IS src/render_frame.cpp NOW, and this is only its Direct2D bracket. The ~600
    // lines that stood here moved out on 2026-10-06, verbatim apart from the two lines below and
    // the stats block's CRT idioms, so the Qt window could draw the same frame: the editor, the
    // start page, the document, and every overlay and dialog. A pixel moving on Windows means the
    // move is wrong, which is what the slice measured.
    app.nativeRenderTarget->BeginDraw();
    renderFrame(app);
    app.nativeRenderTarget->EndDraw();
}

// --- Update check ---
//
// Portable builds ask GitHub for the latest release tag at most once a
// day, on a worker thread that starts ten seconds after startup. Store
// installs update through the Store and skip all of this. The result
// surfaces as a small dismissable chip in the corner, never a dialog.
//
// DISABLED: the worker asks upstream Tinta's releases (oipoistar/tinta), not
// qinta's, so every portable build would be told about someone else's
// release. Flip this back once the request and signals.cpp's release URL
// point at qinta.
static constexpr bool kUpdateCheckEnabled = false;

static bool isStorePackaged() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) !=
           APPMODEL_ERROR_NO_PACKAGE;
}

static std::string todayString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    sprintf_s(buf, _countof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth,
              st.wDay);
    return buf;
}

// "X.Y.Z" newer than the running build?
static bool isNewerVersion(const std::string& version) {
    int major = 0, minor = 0, patch = 0;
    if (sscanf_s(version.c_str(), "%d.%d.%d", &major, &minor, &patch) < 2) {
        return false;
    }
    if (major != TINTA_VERSION_MAJOR) return major > TINTA_VERSION_MAJOR;
    if (minor != TINTA_VERSION_MINOR) return minor > TINTA_VERSION_MINOR;
    return patch > TINTA_VERSION_PATCH;
}

static void updateCheckWorker(HWND hwnd) {
    std::string version;
    HINTERNET session = WinHttpOpen(
        L"tinta-update-check", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        HINTERNET connect = WinHttpConnect(session, L"api.github.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect) {
            HINTERNET request = WinHttpOpenRequest(
                connect, L"GET", L"/repos/oipoistar/tinta/releases/latest",
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (request) {
                if (WinHttpSendRequest(request,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, nullptr)) {
                    std::string body;
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(request, &avail) &&
                           avail > 0 && body.size() < 262144) {
                        size_t at = body.size();
                        body.resize(at + avail);
                        DWORD got = 0;
                        if (!WinHttpReadData(request, &body[at], avail,
                                             &got) ||
                            got == 0) {
                            body.resize(at);
                            break;
                        }
                        body.resize(at + got);
                    }
                    size_t tag = body.find("\"tag_name\"");
                    if (tag != std::string::npos) {
                        size_t colon = body.find(':', tag);
                        size_t open = body.find('"', colon + 1);
                        size_t close = open == std::string::npos
                                           ? std::string::npos
                                           : body.find('"', open + 1);
                        if (close != std::string::npos && close > open + 1) {
                            version = body.substr(open + 1, close - open - 1);
                            if (!version.empty() && version[0] == 'v') {
                                version.erase(0, 1);
                            }
                        }
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connect);
        }
        WinHttpCloseHandle(session);
    }
    auto* result = new std::string(std::move(version));
    if (!postWindowMessage(hwnd, WM_APP_UPDATE_CHECK, 0, (LPARAM)result)) {
        delete result;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;

    switch (msg) {
        case WM_NCCALCSIZE: {
            // Reclaim the caption: the title bar becomes the tab strip.
            // DefWindowProc computes the resize borders, then the top edge
            // is restored so the client area reaches the top of the window.
            if (!wParam || !app || app->zenMode) break;
            NCCALCSIZE_PARAMS* params =
                reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            RECT original = params->rgrc[0];
            DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
            params->rgrc[0].top = original.top;
            if (IsZoomed(hwnd)) {
                params->rgrc[0].top += GetSystemMetrics(SM_CYSIZEFRAME) +
                                       GetSystemMetrics(SM_CXPADDEDBORDER);
            }
            return 0;
        }

        case WM_NCHITTEST: {
            if (!app || app->zenMode) break;
            LRESULT def = DefWindowProcW(hwnd, WM_NCHITTEST, wParam, lParam);
            if (def != HTCLIENT) return def;  // resize borders
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            float x = (float)pt.x, y = (float)pt.y;
            if (y >= chromeTopHeight(*app)) return HTCLIENT;
            if (!IsZoomed(hwnd) &&
                y < (float)GetSystemMetrics(SM_CYSIZEFRAME)) {
                return HTTOP;
            }
            if (appMenuButtonAt(*app, x, y)) return HTCLIENT;
            int button = captionHitTest(*app, x, y);
            if (button == 1) return HTMINBUTTON;
            if (button == 2) return HTMAXBUTTON;  // Win11 snap flyout
            if (button == 3) return HTCLOSE;
            // The floating sheet rises past the strip (design 10a):
            // right of the source column the top band is desk gap and
            // page, both of which take normal clicks
            if (editorPreviewVisible(*app) && x >= editorPaneWidth(*app)) {
                return HTCLIENT;
            }
            for (const App::TabHit& hit : app->tabHits) {
                if (x >= hit.rect.left && x <= hit.rect.right &&
                    y >= hit.rect.top && y <= hit.rect.bottom) {
                    return HTCLIENT;  // tabs, +, chevron take normal clicks
                }
            }
            return HTCAPTION;  // empty strip drags the window
        }

        case WM_NCMOUSEMOVE: {
            if (!app || app->zenMode) break;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            int hover = captionHitTest(*app, (float)pt.x, (float)pt.y);
            if (hover != app->captionButtonHover) {
                app->captionButtonHover = hover;
                requestRepaint(hwnd);
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_NONCLIENT | TME_LEAVE,
                                     hwnd, 0};
            TrackMouseEvent(&track);
            break;
        }

        case WM_NCMOUSELEAVE:
            if (app && (app->captionButtonHover || app->captionButtonPressed)) {
                app->captionButtonHover = 0;
                app->captionButtonPressed = 0;
                requestRepaint(hwnd);
            }
            break;

        case WM_KILLFOCUS:
            if (app && app->showContextMenu) {
                closeContextMenu(*app);
                requestRepaint(hwnd);
            }
            break;

        case WM_NCLBUTTONDOWN:
            if (app && app->showContextMenu) {
                closeContextMenu(*app);
                requestRepaint(hwnd);
            }
            if (app && !app->zenMode &&
                (wParam == HTMINBUTTON || wParam == HTMAXBUTTON ||
                 wParam == HTCLOSE)) {
                app->captionButtonPressed =
                    wParam == HTMINBUTTON ? 1 : wParam == HTMAXBUTTON ? 2 : 3;
                requestRepaint(hwnd);
                return 0;  // consume: no legacy button painting
            }
            break;

        case WM_NCLBUTTONUP:
            if (app && app->captionButtonPressed) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                int released = captionHitTest(*app, (float)pt.x, (float)pt.y);
                int pressed = app->captionButtonPressed;
                app->captionButtonPressed = 0;
                requestRepaint(hwnd);
                if (released == pressed) {
                    if (pressed == 1) {
                        postWindowMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    } else if (pressed == 2) {
                        postWindowMessage(hwnd, WM_SYSCOMMAND,
                                          IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE,
                                          0);
                    } else {
                        postWindowMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
                    }
                }
                return 0;
            }
            break;

        case WM_NCRBUTTONUP:
            if (app && !app->zenMode && wParam == HTCAPTION) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                if (tabContextMenuIndexAt(*app, (float)pt.x, (float)pt.y) >= 0) {
                    // pt is already client-space from the ScreenToClient above,
                    // which is what the handler wants now -- it used to be
                    // handed the raw screen lParam and convert it a second time.
                    handleContextMenu(*app, hwnd, pt.x, pt.y, false);
                    return 0;
                }
                HMENU menu = GetSystemMenu(hwnd, FALSE);
                if (menu) {
                    int cmd = TrackPopupMenu(
                        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                        GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, hwnd,
                        nullptr);
                    if (cmd) postWindowMessage(hwnd, WM_SYSCOMMAND, cmd, 0);
                }
                return 0;
            }
            break;

        case WM_MBUTTONDOWN:
            if (app && app->renderTarget) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                if ((float)my < chromeTopHeight(*app)) {
                    tabStripMouseDown(*app, hwnd, mx, my, true);
                    return 0;
                }
            }
            break;

        case WM_CLOSE:
            // The unsaved-buffer check is input.cpp's windowCloseMayProceed, shared with
            // the Qt window's closeEvent. Falling through lets DefWindowProc destroy.
            if (app && !windowCloseMayProceed(*app, hwnd)) return 0;
            break;

        case WM_COPYDATA:
            if (app) {
                COPYDATASTRUCT* data =
                    reinterpret_cast<COPYDATASTRUCT*>(lParam);
                if (data && data->dwData == 1 && data->lpData &&
                    data->cbData > 0) {
                    std::string path(static_cast<const char*>(data->lpData),
                                     data->cbData - 1);
                    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    tabOpenPath(*app, hwnd, path, true);
                    return TRUE;
                }
            }
            break;

        case WM_SIZE:
            if (app && app->d2dFactory) {
                closeContextMenu(*app);  // its anchor belongs to the old viewport
                // The preview's geometry is stale after a resize: restore
                // the document at the new size and close the overlay
                if (app->showPrintPreview) {
                    app->printSaved.width = LOWORD(lParam);
                    app->printSaved.height = HIWORD(lParam);
                    closePrintPreview(*app, hwnd);
                }
                app->width = LOWORD(lParam);
                app->height = HIWORD(lParam);
                app->clearEditorLineLayoutCache();
                // Resize the target in place: device resources (image and
                // diagram bitmaps) stay valid, unlike a full recreation
                if (!app->renderTarget ||
                    FAILED(app->nativeRenderTarget->Resize(
                        D2D1::SizeU(app->width, app->height)))) {
                    createRenderTarget(*app);
                }
                app->layoutDirty = true;
                requestRepaint(hwnd);
            }
            return 0;

        case WM_SETTINGCHANGE:
            // Windows switched light/dark mode
            if (app && lParam && wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
                followSystemAppearance(*app, systemPrefersLight());
            }
            return 0;

        case WM_DPICHANGED:
            if (app) {
                if (app->showPrintPreview) closePrintPreview(*app, hwnd);
                UINT dpi = HIWORD(wParam);
                app->contentScale = dpi / 96.0f;

                // Resize window to suggested new size
                RECT* newRect = (RECT*)lParam;
                SetWindowPos(hwnd, nullptr,
                    newRect->left, newRect->top,
                    newRect->right - newRect->left,
                    newRect->bottom - newRect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);

                // Recreate text formats and render target for new DPI
                updateTextFormats(*app);
                createRenderTarget(*app);
                requestRepaint(hwnd);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (app) render(*app);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // The five mouse handlers, WM_KEYDOWN, WM_CHAR, WM_DROPFILES and
        // WM_CONTEXTMENU below take DATA, not message parameters -- decoding a
        // message is this function's job, because this function is the only
        // thing in the app that has one. That keeps <windowsx.h> here, where it
        // belongs and where the Qt cutover deletes it, instead of in
        // src/input.cpp where it was the fatal include blocking the port.
        case WM_MOUSEWHEEL:
            if (app) {
                handleMouseWheel(*app, hwnd,
                                 (float)GET_WHEEL_DELTA_WPARAM(wParam) /
                                     WHEEL_DELTA);
            }
            return 0;

        case WM_MOUSEHWHEEL:
            if (app) {
                handleMouseHWheel(*app, hwnd,
                                  (float)GET_WHEEL_DELTA_WPARAM(wParam) /
                                      WHEEL_DELTA);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (app) {
                handleMouseMove(*app, hwnd, GET_X_LPARAM(lParam),
                                GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_LBUTTONDOWN:
            // wParam carries the button and modifier state; neither handler
            // ever read it -- they ask GetKeyState and app state instead -- so
            // it is dropped here rather than passed on unused.
            if (app) {
                handleMouseDown(*app, hwnd, GET_X_LPARAM(lParam),
                                GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_LBUTTONUP:
            if (app) {
                handleMouseUp(*app, hwnd, GET_X_LPARAM(lParam),
                              GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            if (app) {
                app->windowMoveTracking = true;
                GetWindowRect(hwnd, &app->windowMoveStartRect);
            }
            break;

        case WM_EXITSIZEMOVE:
            // A finished native window MOVE may be a drop onto another
            // Tinta window's strip; resizes land here too, so only a
            // changed position with an unchanged size qualifies
            if (app && app->windowMoveTracking) {
                app->windowMoveTracking = false;
                RECT now;
                const RECT& was = app->windowMoveStartRect;
                if (GetWindowRect(hwnd, &now) &&
                    (now.left != was.left || now.top != was.top) &&
                    now.right - now.left == was.right - was.left &&
                    now.bottom - now.top == was.bottom - was.top) {
                    tabWindowDropMerge(*app, hwnd);
                }
            }
            break;

        case WM_CAPTURECHANGED:
            // Losing capture mid tab-drag (Alt+Tab, a popup stealing the
            // mouse) aborts the drag instead of leaving a stray ghost
            if (app && app->tabDragIndex >= 0 && (HWND)lParam != hwnd) {
                tabDragCancel(*app, hwnd);
            }
            return 0;

        case WM_SETCURSOR:
            if (app && LOWORD(lParam) == HTCLIENT) {
                // We handle cursor in WM_MOUSEMOVE
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
            if (app) return handleKeyDown(*app, hwnd, (unsigned)wParam) ? 1 : 0;
            return 0;

        case WM_XBUTTONUP:
            // Mouse side buttons walk back/forward across link jumps
            if (app) {
                if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
                    navigateBack(*app, hwnd);
                } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
                    navigateForward(*app, hwnd);
                }
            }
            return TRUE;

        case WM_SYSKEYDOWN:
            if (app && wParam == VK_F10) {
                handleKeyDown(*app, hwnd, (unsigned)wParam);
                return 0;
            }
            // Alt+Left / Alt+Right mirror the mouse side buttons
            if (app && (lParam & (1 << 29)) &&
                (wParam == VK_LEFT || wParam == VK_RIGHT)) {
                if (wParam == VK_LEFT) navigateBack(*app, hwnd);
                else navigateForward(*app, hwnd);
                return 0;
            }
            break;

        case WM_CHAR:
            if (app) handleCharInput(*app, hwnd, (wchar_t)wParam);
            return 0;

        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
            // Anchor the IME composition/candidate window at the caret in
            // edit mode, then let DefWindowProc run default IME handling
            if (app && app->editMode) editorPositionImeWindow(*app, hwnd);
            break;

        case WM_DROPFILES:
            if (app) {
                // Enumerate the drop here and hand over plain paths: the
                // handler decides which ones it supports, and this owns the
                // handle, so DragFinish runs whatever the handler does --
                // including throwing every path away.
                HDROP hDrop = (HDROP)wParam;
                UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                std::vector<std::wstring> paths;
                paths.reserve(count);
                for (UINT i = 0; i < count; i++) {
                    wchar_t wpath[MAX_PATH];
                    // A NUL-terminated MAX_PATH buffer, so the wstring stops at
                    // the terminator exactly as the -1 form did.
                    if (DragQueryFileW(hDrop, i, wpath, MAX_PATH)) {
                        paths.emplace_back(wpath);
                    }
                }
                DragFinish(hDrop);
                handleDropFiles(*app, hwnd, paths);
            }
            return 0;

        case WM_TIMER:
            // The dispatch is input.cpp's handleTimer, shared with the Qt window's
            // timer sink. The update check stays here: its worker is this file's.
            if (wParam == TIMER_UPDATE_CHECK && app) {
                disarmTimer(hwnd, TIMER_UPDATE_CHECK);
                std::thread(updateCheckWorker, hwnd).detach();
            }
            if (app) handleTimer(*app, hwnd, (unsigned)wParam);
            return 0;

        case WM_APP_IMAGE_READY:
            // A background image download finished: swap it into the cache
            // and reflow (handler takes ownership of the result)
            if (app) completeAsyncImage(*app, (void*)lParam);
            return 0;

        case WM_CONTEXTMENU:
            if (app) {
                // Win32 spells "this came from the menu key, not a click" as a
                // (-1, -1) screen position, and the handler used to test for
                // that itself. Both halves of the decode are here now, so the
                // handler receives client coordinates in every case.
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                bool fromKeyboard = pt.x == -1 && pt.y == -1;
                if (!fromKeyboard) ScreenToClient(hwnd, &pt);
                handleContextMenu(*app, hwnd, pt.x, pt.y, fromKeyboard);
            }
            return 0;

        case WM_APP_FOLDER_SEARCH:
            // Folder search worker finished (handler takes ownership)
            if (app) completeFolderSearch(*app, (void*)lParam);
            return 0;

        case WM_APP_UPDATE_CHECK: {
            // Update worker finished (handler takes ownership). A failed
            // check does not stamp the day, so it retries next launch.
            auto* version = (std::string*)lParam;
            if (app && version && !version->empty()) {
                app->updateLastCheck = todayString();
                if (isNewerVersion(*version) &&
                    *version != app->updateDismissedVersion) {
                    app->updateVersion = *version;
                    app->updateAvailable = true;
                    wchar_t label[96];
                    swprintf_s(label, _countof(label),
                               tr(*app, "update.available"),
                               toWide(*version).c_str());
                    signalPush(*app, SIG_INFO, SIGI_UPDATE, label, L"",
                               L"v" + toWide(*version), SIGA_OPEN_RELEASE,
                               SIGC_DISMISS_UPDATE, false);
                    requestRepaint(hwnd);
                }
            }
            delete version;
            return 0;
        }

        case WM_APP_PANDOC_DONE:
            if (app) {
                app->pandocRunning = false;
                if (wParam) {
                    signalPushKey(*app, SIG_SUCCESS, SIGI_CHECK,
                                  "toast.pandoc_ok");
                } else {
                    signalPushKey(*app, SIG_ERROR, SIGI_WARNING,
                                  "toast.pandoc_fail");
                }
                requestRepaint(hwnd);
            }
            return 0;

        case WM_APP_GPU_READY:
            // Driver is warm: swap the startup software render target for a
            // hardware one. The recreated target's own device keeps the
            // driver initialized, so the warm-up device can go.
            if (app && !app->useHardwareRT) {
                app->useHardwareRT = true;
                createRenderTarget(*app);
                app->layoutDirty = true;
                requestRepaint(hwnd);
            }
            if (g_warmupDevice) {
                g_warmupDevice->Release();
                g_warmupDevice = nullptr;
            }
            return 0;

        case WM_APP_LAYOUT_CHUNK:
            // src/input.cpp's handlePostedMessage, which the Qt window's post sink calls too.
            if (app) handlePostedMessage(*app, msg, wParam, lParam);
            return 0;

        case WM_DESTROY:
            disarmTimer(hwnd, TIMER_FILE_WATCH);
            disarmTimer(hwnd, 2); // TIMER_EDITOR_REPARSE
            disarmTimer(hwnd, TIMER_CURSOR_BLINK);
            disarmTimer(hwnd, TIMER_NOTIFICATION);
            disarmTimer(hwnd, TIMER_ZOOM_APPLY);
            disarmTimer(hwnd, TIMER_DRAFT_SAVE);
            // A graceful close resolved every dirty buffer through the
            // unsaved-changes flow; leftover drafts would resurrect
            // content the user already decided about
            if (app) draftsDeleteAll(*app);
            {
                // Load existing settings to preserve values like hasAskedFileAssociation
                Settings settings = loadSettings();
                settings.themeIndex = app->currentThemeIndex;
                settings.zoomFactor = app->zoomFactor;
                settings.editorShowPreview = app->editorShowPreview;
                settings.editorWordWrap = app->editorWordWrap;
                settings.editorAssists = app->editorAssists;
                settings.followSystemTheme = app->followSystemTheme;
                settings.lightThemeIndex = app->lightThemeIndex;
                settings.darkThemeIndex = app->darkThemeIndex;
                settings.checkUpdates = app->updateCheckEnabled;
                settings.lastUpdateCheck = app->updateLastCheck;
                settings.dismissedUpdate = app->updateDismissedVersion;
                settings.folderSearchEnabled = app->folderSearchEnabled;
                settings.browserFocusPath = app->browserFocusPath;
                // openInTabs persists at toggle time (persistOpenInTabs)
                // and is preserved from disk here: a window opened before
                // the toggle must not write its stale value back at exit
                // Remember the open tab set for the next launch (untitled
                // quick-note buffers have no path to restore). Satellite
                // windows never own the session: they re-read the owner's
                // stored session so their full-file save cannot clobber a
                // newer one with launch-time data.
                if (app->sessionOwner) {
                    settings.sessionTabs.clear();
                    settings.sessionActive = 0;
                    for (size_t i = 0; i < app->tabs.size(); i++) {
                        const std::string& path =
                            (int)i == app->activeTab ? app->currentFile
                                                     : app->tabs[i].path;
                        if (path.empty()) continue;
                        if ((int)i == app->activeTab) {
                            settings.sessionActive =
                                (int)settings.sessionTabs.size();
                        }
                        settings.sessionTabs.push_back(path);
                    }
                } else {
                    Settings onDisk = loadSettings();
                    settings.sessionTabs = onDisk.sessionTabs;
                    settings.sessionActive = onDisk.sessionActive;
                }
                settings.readingWidthPct = app->readingWidthPct;
                settings.zenWidthPct = app->zenWidthPct;
                settings.headingRules = app->headingRules;
                settings.tocOnLeft = app->tocOnLeft;
                settings.tocPinned = app->tocPinned;
                settings.browserPinned = app->browserPinned;
                settings.language = app->languageSetting >= 0
                    ? languageIdAt(app->languageSetting) : "auto";
                settings.keyProfile = app->keyProfile;
                settings.keyOverrides = app->keyOverrides;
                if (!app->currentFile.empty()) {
                    rememberReadingPosition(settings, app->currentFile,
                                            app->scrollY, app->zoomFactor);
                }

                // Get window placement for position/size/maximized state
                WINDOWPLACEMENT wp = {};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp)) {
                    settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
                    // Save the restored (non-maximized) position
                    settings.windowX = wp.rcNormalPosition.left;
                    settings.windowY = wp.rcNormalPosition.top;
                    settings.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                    settings.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                }

                saveSettings(settings);
            }
            // Routed for consistency rather than for portability -- this file is Windows-only. See
            // win32_api.h: the seam's comment claims three sites, and a site left spelling the raw
            // call would make that claim false the moment somebody counted.
            requestQuit();
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The bare-launch tutorial moved to startpage.cpp: failed loads fall
// back to the start page, which keeps the sample behind a Learn card

// Last-chance handler: writes %LOCALAPPDATA%\Tinta\crash.dmp so crash
// reports from the field carry a usable stack
static LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exceptionInfo) {
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return EXCEPTION_CONTINUE_SEARCH;
    std::wstring dir = std::wstring(path) + L"\\Tinta";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\crash.dmp";
    HANDLE dump = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info;
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exceptionInfo;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                          MiniDumpWithIndirectlyReferencedMemory, &info,
                          nullptr, nullptr);
        CloseHandle(dump);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(writeCrashDump);
    // Enable per-monitor DPI V2 awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto startupStart = Clock::now();

    App app;
    auto t0 = startupStart;
    g_app = &app;

    // Load user themes before settings: saved theme indices may point at them
    loadCustomThemes();
    loadLanguageOverrides();

    // Load saved settings
    Settings savedSettings = loadSettings();
    app.followSystemTheme = savedSettings.followSystemTheme;

    // Update check state (the timer arms after the window exists)
    app.updateCheckEnabled = savedSettings.checkUpdates;
    app.updateLastCheck = savedSettings.lastUpdateCheck;
    app.updateDismissedVersion = savedSettings.dismissedUpdate;
    app.lightThemeIndex = savedSettings.lightThemeIndex;
    app.darkThemeIndex = savedSettings.darkThemeIndex;
    app.folderSearchEnabled = savedSettings.folderSearchEnabled;
    app.browserFocusPath = savedSettings.browserFocusPath;
    app.openInTabs = savedSettings.openInTabs;
    app.editorAssists = savedSettings.editorAssists;
    app.pandocUserPath = toWide(savedSettings.pandocPath);
    int startTheme = app.followSystemTheme ? autoThemeIndex(app)
                                           : savedSettings.themeIndex;
    app.currentThemeIndex = startTheme;
    if (startTheme >= themeCount()) startTheme = 0;
    app.currentThemeIndex = startTheme;
    app.theme = themeAt(startTheme);
    app.darkMode = app.theme.isDark;
    app.zoomFactor = savedSettings.zoomFactor;
    app.editorShowPreview = savedSettings.editorShowPreview;
    app.editorWordWrap = savedSettings.editorWordWrap;
    app.readingWidthPct = savedSettings.readingWidthPct;
    app.zenWidthPct = savedSettings.zenWidthPct;
    app.headingRules = savedSettings.headingRules;
    app.tocOnLeft = savedSettings.tocOnLeft;
    app.tocPinned = savedSettings.tocPinned;
    app.browserPinned = savedSettings.browserPinned;
    app.languageSetting = savedSettings.language == "auto"
        ? -1 : languageIndexById(savedSettings.language);
    app.currentLanguageIndex = app.languageSetting >= 0
        ? app.languageSetting : detectSystemLanguage();
    app.keyOverrides = savedSettings.keyOverrides;
    applyKeymap(app, savedSettings);

    // Parse command line
    std::string inputFile;
    bool lightMode = false;
    bool forceRegister = false;
    bool cascadeWindow = false;   // offset from the saved position (new-file windows)
    bool startInEditMode = false; // open straight into the editor
    bool quickNote = false;       // Ctrl+N: untitled buffer, no backing file
    std::wstring printPagesDir;   // debug: render print pages as PNGs and exit
    std::wstring exportHtmlPath;  // debug: write the HTML export and exit
    // debug: characterise the forty chrome text formats and exit. The falsifier for the
    // chrome half of the port, because --printpages above cannot see any of it: chrome
    // never appears on a printed page. See include/chrome_dump.h.
    std::wstring dumpChromePath;
    std::wstring exportDocxPath;  // debug: write the DOCX export and exit
    std::wstring exportPdfPath;   // debug: write the PDF export and exit
    int posX = 0, posY = 0;       // --pos: spawn position (tab drag-out)
    bool hasPos = false;

    // THE OPTION CHAIN THAT STOOD HERE IS NOW include/launch_options.h, and this is the whole
    // of what is left on the Windows side: widen to UTF-8, parse, assign. The chain was 36
    // lines and src/main_qt.cpp had no equivalent at all -- not a second parser that agreed,
    // but one parser and one file that had never been told the options existed, which is why
    // `tinta --light a.md` on the Qt arm opened nothing and complained about a missing file
    // called "--light".
    //
    // CommandLineToArgvW STAYS, and for the same reason EnumDisplayMonitors stays below:
    // splitting one command-line string into argv by the Win32 quoting rule is genuinely
    // platform work. What moved is the DECISION about what the resulting tokens mean.
    //
    // THE UTF-8 ROUND TRIP IS NOT NEW. The document path already made this exact trip --
    // platform::toUtf8 on the way in here and toWide again a few lines below, where the
    // single-instance handoff needs a wide path -- so widening the other four path options
    // alongside it adds no conversion the shipping path did not already perform.
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> launchArgs;
    if (argc > 1) {
        launchArgs.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; i++) launchArgs.push_back(platform::toUtf8(argv[i]));
    }
    LocalFree(argv);

    const tinta::LaunchOptions options = tinta::parseLaunchOptions(launchArgs);
    inputFile = options.inputFile;
    lightMode = options.lightMode;
    app.showStats = options.showStats;
    forceRegister = options.forceRegister;
    cascadeWindow = options.cascade;
    app.forceTabStrip = options.forceTabStrip;  // drag-out satellites keep the tab row
    startInEditMode = options.startInEditMode;
    quickNote = options.quickNote;
    if (!options.printPagesDir.empty()) printPagesDir = toWide(options.printPagesDir);
    if (!options.dumpChromePath.empty()) dumpChromePath = toWide(options.dumpChromePath);
    if (!options.exportHtmlPath.empty()) exportHtmlPath = toWide(options.exportHtmlPath);
    if (!options.exportDocxPath.empty()) exportDocxPath = toWide(options.exportDocxPath);
    if (!options.exportPdfPath.empty()) exportPdfPath = toWide(options.exportPdfPath);
    hasPos = options.hasPos;
    posX = options.posX;
    posY = options.posY;

    // A BAD FLAG IS SAID OUT LOUD, and this comment used to say the opposite. What stood here
    // claimed both vectors were unreportable because "this target is built with the WIN32 flag
    // so it runs on the GUI subsystem and has no stderr to write to". That confuses NO CONSOLE
    // with NO STDERR. A GUI-subsystem process is not given a console, but it inherits whatever
    // standard handles its parent passes, so a redirect works normally. Measured rather than
    // argued, on the sibling target, which is built WIN32 for the same reason: running
    // `qinta.exe C:/definitely/not/a/file.md 2> err` captured its 50-byte "cannot open"
    // message, and the same run against a real path captured 0 bytes -- so the channel exists
    // and it is the message that varies, not the plumbing.
    //
    // So writing here costs nothing and is silent in the case that motivated the old comment:
    // double-clicked from Explorer there is no handle, the write goes nowhere, and no window
    // appears to nag about a typo. Redirected from a shell or a test, it is readable. That is
    // strictly better than recording a typo in a vector nobody prints. A MESSAGE BOX would
    // still be worse than silence -- that half of the old reasoning survives, and it is the
    // half that was actually about judgement rather than about plumbing.
    for (const std::string& flag : options.unknown) {
        std::fprintf(stderr, "tinta: unknown option '%s'\n", flag.c_str());
    }
    for (const std::string& flag : options.missingValues) {
        std::fprintf(stderr, "tinta: option '%s' needs a value\n", flag.c_str());
    }

    // AND NOW ACT ON WHAT THOSE TWO LOOPS JUST REPORTED, which is the whole of this guard. The
    // loops above have printed since the seam landed; what they did next was fall through to the
    // export dispatch ~340 lines down and write the file anyway. On 2026-09-20 that turned
    // `tinta --lite --exportpdf README.md` into a 4.4 MB PDF where a tracked 2,357-line README had
    // been, at exit 0, with the `--lite` typo as the only complaint on the channel. The facts
    // needed to refuse were all in `options` at this point in the function.
    //
    // PLACED HERE, at the earliest point where `options` exists, rather than beside the dispatch:
    // there is no window yet, no App, no document loaded, and nothing to unwind -- so the refusal
    // is a print and a return. Down beside the writes it would sit after a full startup and would
    // have to be repeated in five branches, which is five chances to add a sixth without it.
    {
        const tinta::PlannedOutput planned = tinta::plannedOutput(options);
        if (!planned.flag.empty()) {
            // regularFileExists is false for a directory, which is what --printpages wants: a
            // folder is not a document, so that arm is guarded by the other two tests only.
            const bool exists =
                platform::regularFileExists(toWide(planned.path));
            const tinta::OutputRefusal refusal = tinta::refuseOutput(options, exists);
            if (refusal != tinta::OutputRefusal::None) {
                std::fprintf(stderr, "tinta: refusing to write '%s' -- %s\n",
                             planned.path.c_str(),
                             tinta::outputRefusalMessage(refusal));
                std::fprintf(stderr,
                             "tinta: %s names the file to WRITE, not the file to read\n",
                             planned.flag.c_str());
                return 2;
            }
        }
    }

    // Single instance: a plain file launch joins the existing window as a
    // new tab (Win11 Notepad model). --new/--cascade windows and the
    // openInTabs=false setting keep the one-window-per-document behavior.
    if (!inputFile.empty() && !quickNote && !cascadeWindow &&
        printPagesDir.empty() && dumpChromePath.empty() && exportHtmlPath.empty() &&
        exportDocxPath.empty() && exportPdfPath.empty() &&
        savedSettings.openInTabs) {
        HWND existing = FindWindowW(L"Tinta", nullptr);
        if (existing) {
            // Resolve to an absolute path: the receiving window has its own
            // working directory
            std::wstring wide = toWide(inputFile);
            wchar_t full[MAX_PATH];
            if (GetFullPathNameW(wide.c_str(), MAX_PATH, full, nullptr)) {
                // full is NUL-terminated, so constructing the wstring stops
                // there -- same result the explicit -1 conversion produced.
                std::string absolute = platform::toUtf8(full);
                COPYDATASTRUCT data;
                data.dwData = 1;
                data.cbData = (DWORD)absolute.size() + 1;
                data.lpData = (void*)absolute.c_str();
                SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&data);
                if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
                return 0;
            }
        }
    }

    // Handle /register command
    if (forceRegister) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       tr(app, "fileassoc.done_body"),
                       tr(app, "fileassoc.done_title"),
                       MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, tr(app, "fileassoc.register_failed_body"),
                       tr(app, "error.title"), MB_OK | MB_ICONWARNING);
        }
        return 0;  // Exit after registering
    }

    // Ask about file association on first run
    askAndRegisterFileAssociation(savedSettings);

    if (lightMode) {
        // The index is kLightModeThemeIndex rather than a literal 0 because src/main_qt.cpp
        // now makes the same selection, and "light means slot 0" typed in two files is the
        // drift. darkMode is read OFF the theme instead of hardcoded false for the same
        // reason: the old `= false` was correct only as long as slot 0 stays light, which is
        // precisely the thing the constant exists to let somebody change.
        app.currentThemeIndex = kLightModeThemeIndex;
        app.theme = themeAt(kLightModeThemeIndex);
        app.darkMode = app.theme.isDark;
    }

    // Create window with saved position/size
    t0 = Clock::now();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, L"IDI_ICON1");
    wc.hIconSm = LoadIconW(hInstance, L"IDI_ICON1");
    wc.lpszClassName = L"Tinta";
    RegisterClassExW(&wc);

    // WHERE THE WINDOW GOES IS DECIDED IN src/window_placement.cpp, NOT HERE, and the split
    // is between ENUMERATING the displays -- genuinely platform work, below -- and CHOOSING
    // among them, which is arithmetic over rectangles and now has a test. What used to sit
    // here was thirty-five lines doing both: the #25 clamp (a position saved on a
    // since-disconnected screen would otherwise restore off-screen), the --pos override and
    // the --cascade step, none of it reachable by anything because it was inside WinMain.
    //
    // MonitorFromRect SURVIVES, and its survival is the point of the boundary: it answers
    // "which monitor" and only the OS can. What it is no longer allowed to do is decide.
    std::vector<tinta::MonitorArea> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM out) -> BOOL {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(monitor, &mi)) {
                tinta::MonitorArea area;
                area.bounds = { mi.rcMonitor.left, mi.rcMonitor.top,
                                mi.rcMonitor.right - mi.rcMonitor.left,
                                mi.rcMonitor.bottom - mi.rcMonitor.top };
                area.work = { mi.rcWork.left, mi.rcWork.top,
                              mi.rcWork.right - mi.rcWork.left,
                              mi.rcWork.bottom - mi.rcWork.top };
                reinterpret_cast<std::vector<tinta::MonitorArea>*>(out)->push_back(area);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    tinta::PlacementRequest placement;
    placement.savedX = savedSettings.windowX;
    placement.savedY = savedSettings.windowY;
    placement.savedWidth = savedSettings.windowWidth;
    placement.savedHeight = savedSettings.windowHeight;
    placement.cascade = cascadeWindow;
    placement.hasPos = hasPos;
    placement.posX = posX;
    placement.posY = posY;

    const tinta::WindowRect chosen = tinta::choosePlacement(placement, monitors);
    // kWindowPositionUseDefault IS CW_USEDEFAULT -- app.h static_asserts it, which is what
    // lets the sentinel cross the seam as a plain int and arrive here as the macro again.
    const int windowX = chosen.x;
    const int windowY = chosen.y;

    // chosen.width/height ARE DISCARDED HERE, DELIBERATELY, AND IT PRESERVES A BUG.
    // CreateWindowExW below is passed savedSettings.windowWidth/windowHeight, which is what
    // shipped: the old code computed a shrunk size, used it to clamp the POSITION, and then
    // created the window at the full saved size anyway -- so a window clamped into a smaller
    // monitor comes back on-screen at the top-left and still overhangs the bottom-right.
    // That is a real defect and it is NOT fixed in this commit, because this commit's claim
    // is that placement behaviour is UNCHANGED; fixing it here would make that claim
    // unfalsifiable by burying a behaviour change inside a refactor. The seam now returns the
    // right size, so the fix is a two-line follow-up with a test already in place for it.

    // Only the primary window persists and restores the tab session;
    // deliberate offshoots (new-note windows, drag-outs) are satellites
    app.sessionOwner = !cascadeWindow && !quickNote;

    app.hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"Tinta",
        L"Tinta",
        WS_OVERLAPPEDWINDOW,
        windowX, windowY,
        savedSettings.windowWidth, savedSettings.windowHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    app.metrics.windowInitUs = usElapsed(t0);

    // Get DPI using per-monitor aware API
    app.contentScale = GetDpiForWindow(app.hwnd) / 96.0f;

    // Theme the native title bar before the window is ever shown
    applyWindowChrome(app);

    // Apply the custom-caption frame (WM_NCCALCSIZE) to the fresh window
    SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);

    // Initialize D2D
    if (!initD2D(app)) {
        MessageBoxW(nullptr, tr(app, "error.d2d_init_failed"),
                   tr(app, "error.title"), MB_OK);
        return 1;
    }

    // Create text formats and typography. Both are portable calls now
    // (include/text_formats.h) and the Qt path makes the same pair from
    // installQtBackends, which is the only place either one is called.
    updateTextFormats(app);
    installTypography(app);

    // Get window size
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    app.width = rc.right - rc.left;
    app.height = rc.bottom - rc.top;

    // Create render target
    t0 = Clock::now();
    if (!createRenderTarget(app)) {
        MessageBoxW(nullptr, tr(app, "error.render_target_failed"),
                   tr(app, "error.title"), MB_OK);
        return 1;
    }
    app.metrics.renderTargetUs = usElapsed(t0);

    // Load document
    t0 = Clock::now();

    auto loadDocumentContent = [&](const std::string& content, std::string_view path) {
        auto result = parseDocument(app.parser, content, path);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
            // Review annotations derive from the raw source (#126)
            app.sourceText = content;
            annotationsParseSource(app);
        }
        return result.success;
    };

    auto loadFile = [&](const std::string& path) -> bool {
        // Use wide string path for ifstream to support non-ASCII paths (MSVC extension)
        std::wstring widePath = toWide(path);
        std::ifstream file(widePath);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return loadDocumentContent(buffer.str(), path);
    };

    // Last session's tabs come back on plain launches (files that vanished
    // from disk in the meantime are dropped). With "Open files in tabs"
    // off the session stays parked: one window, one document — a file
    // launch must not drag the previous session in as extra tabs (#147)
    bool restoreSession = !quickNote && !cascadeWindow && !startInEditMode &&
                          printPagesDir.empty() && savedSettings.openInTabs &&
                          !savedSettings.sessionTabs.empty();
    std::vector<std::string> sessionPaths;
    int sessionActive = 0;
    if (restoreSession) {
        for (size_t i = 0; i < savedSettings.sessionTabs.size(); i++) {
            const auto& path = savedSettings.sessionTabs[i];
            if (GetFileAttributesW(toWide(path).c_str()) !=
                INVALID_FILE_ATTRIBUTES) {
                // The active slot follows its path through the filtering
                if ((int)i == savedSettings.sessionActive) {
                    sessionActive = (int)sessionPaths.size();
                }
                sessionPaths.push_back(path);
            }
        }
        if (sessionPaths.empty()) restoreSession = false;
    }

    // Windows that come up with nothing to show land on the start page
    // (design t7) instead of the old tutorial document: an empty
    // document is all it takes, the launcher is the empty state
    auto showStartPage = [&]() { loadDocumentContent(std::string(), {}); };

    if (quickNote) {
        // Untitled quick note: an empty document, no backing file
        loadDocumentContent(std::string(), {});
    } else if (!inputFile.empty()) {
        if (loadFile(inputFile)) {
            app.currentFile = inputFile;
            app.focusMermaidOnNextLayout = isMermaidDocumentPath(inputFile);
            persistRecentFile(app.currentFile);
        } else {
            showStartPage();
        }
    } else if (restoreSession) {
        // No file argument: reopen where the last session left off
        if (loadFile(sessionPaths[sessionActive])) {
            app.currentFile = sessionPaths[sessionActive];
            app.focusMermaidOnNextLayout =
                isMermaidDocumentPath(sessionPaths[sessionActive]);
        } else {
            showStartPage();
        }
    } else {
        // Try syntax.md
        if (loadFile("syntax.md")) {
            app.currentFile = "syntax.md";
        } else {
            showStartPage();
        }
    }

    app.metrics.fileLoadUs = usElapsed(t0);

    // Resume the saved reading position (#77); new-file windows start in the
    // editor at the top instead
    if (!startInEditMode && !app.currentFile.empty()) {
        app.pendingScrollRestore = findReadingPosition(savedSettings, app.currentFile);
        // Per-document zoom overrides the global one for known documents
        float docZoom = findReadingZoom(savedSettings, app.currentFile);
        if (docZoom > 0.0f && fabsf(docZoom - app.zoomFactor) > 0.01f) {
            app.zoomFactor = docZoom;
            app.appliedZoomFactor = docZoom;
            updateTextFormats(app);
            app.layoutDirty = true;
        }
    }

    // Set window title with filename
    updateWindowTitle(app);

    // The startup document becomes the first (tabless) tab; a restored
    // session rebuilds the whole row in its saved order
    if (restoreSession && !app.currentFile.empty()) {
        tabsSeedSession(app, sessionPaths);
    } else {
        tabsInit(app);
    }

    // Start file watch timer and record initial write time
    updateFileWriteTime(app);
    armTimer(app.hwnd, TIMER_FILE_WATCH, 500);

    // Show window (respect saved maximized state)
    t0 = Clock::now();
    if (savedSettings.windowMaximized) {
        ShowWindow(app.hwnd, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(app.hwnd, nCmdShow);
    }
    UpdateWindow(app.hwnd);
    app.metrics.showWindowUs = usElapsed(t0);

    // Update check: portable builds only, at most once a day, and only
    // long after startup has finished (10 s timer arms a worker thread).
    // Off until it is re-pointed at qinta -- see kUpdateCheckEnabled.
    if (kUpdateCheckEnabled && app.updateCheckEnabled && !isStorePackaged() &&
        app.updateLastCheck != todayString()) {
        armTimer(app.hwnd, TIMER_UPDATE_CHECK, 10000);
    }

    // Draft autosave sweep (crash recovery), and the offer to restore
    // whatever a crashed instance left behind
    armTimer(app.hwnd, TIMER_DRAFT_SAVE, DRAFT_SAVE_INTERVAL_MS);
    draftsScanForRecovery(app);

    app.metrics.totalStartupUs = usElapsed(startupStart);

    // New-file windows open straight into the editor
    if (quickNote) {
        enterQuickNoteMode(app);
    } else if (startInEditMode) {
        enterEditMode(app);
    }

    // Debug: characterise the chrome text formats and exit.
    //
    // Placed HERE, beside the other harness switches, rather than straight after
    // updateTextFormats() forty lines up where the ten formats first exist -- the point is
    // to read the formats the running app would actually use, at the scale and zoom a full
    // startup settled on. ensureThemePreviewFormats is called explicitly because it is the
    // lazy door the theme chooser opens: without it the thirty preview formats are simply
    // absent, and thirty absent rows must not be mistaken for thirty unchanged ones.
    if (!dumpChromePath.empty()) {
        ensureThemePreviewFormats(app);
        int rows = dumpChromeFormats(app, dumpChromePath);
        wchar_t msg[64];
        swprintf_s(msg, L"dumpchrome: %d", rows);
        OutputDebugStringW(msg);
        // The exact expected count, and not "more than zero". A partial dump is the failure
        // mode that would read as a pass: ten rows written before a bad path or an unopened
        // lazy door would still be a file with content in it, and a diff of two ten-row
        // files is green. DERIVED from themeCount() rather than written as 40, so adding a
        // theme does not turn this into a silent exit 1.
        const int expectedRows = 10 + 3 * themeCount();
        return rows == expectedRows ? 0 : 1;
    }

    // Debug: rasterize the print pagination to PNGs and exit
    if (!printPagesDir.empty()) {
        int pages = printDebugPages(app, printPagesDir);
        wchar_t msg[64];
        swprintf_s(msg, L"printpages: %d", pages);
        OutputDebugStringW(msg);
        return pages > 0 ? 0 : 1;
    }

    // Debug: write the HTML export and exit (#export_as harness)
    if (!exportHtmlPath.empty()) {
        bool ok = exportHtmlFile(app, exportHtmlPath);
        return ok ? 0 : 1;
    }
    if (!exportDocxPath.empty()) {
        bool ok = exportDocxFile(app, exportDocxPath);
        return ok ? 0 : 1;
    }
    if (!exportPdfPath.empty()) {
        bool ok = exportPdfFile(app, exportPdfPath);
        return ok ? 0 : 1;
    }

    // Pinned side panels are part of the layout, so they come back on
    // launch when a document is up (#156). The start page, quick notes,
    // and --edit launches keep their clean slate; T/B still toggle.
    if (!app.currentFile.empty() && !app.editMode) {
        if (app.tocPinned) {
            app.showToc = true;
            app.tocAnimation = 1.0f;  // docked: no slide on restore
            app.tocScroll = 0;
            app.tocFilter.clear();
            app.hoveredTocIndex = -1;
        }
        if (app.browserPinned) {
            app.showFolderBrowser = true;
            app.folderBrowserAnimation = 1.0f;
            app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
            populateFolderItems(app);
        }
        if (app.tocPinned || app.browserPinned) {
            requestRepaint(app.hwnd);
        }
    }

    // First frame is on screen — now pay for the GPU in the background
    startGpuWarmup();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == app.hwnd && msg.message == WM_KEYDOWN) {
            // Give command shortcuts the first chance to consume a key.
            // Only unconsumed keystrokes generate WM_CHAR; no timed input
            // suppression is needed when E opens the editor (#195).
            if (!DispatchMessage(&msg)) TranslateMessage(&msg);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_app = nullptr;
    return (int)msg.wParam;
}

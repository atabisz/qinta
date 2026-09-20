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
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "render.h"
#include "paint_document.h"
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

// The theme the auto mode wants right now
static int autoThemeIndex(const App& app) {
    return systemPrefersLight() ? app.lightThemeIndex : app.darkThemeIndex;
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);

void render(App& app) {
    if (!app.renderTarget) return;

    app.nativeRenderTarget->BeginDraw();
    app.drawCalls = 0;

    // Print preview replaces the whole frame: the document is in print
    // layout while it is open, so the normal paths would draw nonsense
    if (app.showPrintPreview) {
        renderPrintPreview(app);
        app.nativeRenderTarget->EndDraw();
        return;
    }

    // Any viewport width change (folder browser toggle, preview split)
    // re-flows the reading column for the space that remains
    {
        float vw = documentViewportWidth(app);
        if (vw > 0.0f && vw != app.lastViewportWidth) {
            app.lastViewportWidth = vw;
            app.layoutDirty = true;
        }
    }

    if (app.layoutDirty) {
        if (app.editMode && !editorPreviewVisible(app)) {
            // Preview hidden: defer document layout until it's shown again
            // (the viewport is zero-width, so laying out now would be wasted
            // work against a nonsense max width)
        } else if (app.editMode) {
            // The preview streams in like the viewer: visible region first,
            // the rest in background chunks. The anchor sync clamps to
            // whatever is laid out and self-corrects as chunks land, so
            // entering edit mode no longer blocks on a full layout (#77)
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                postWindowMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        } else {
            // Lay out the visible region first so this frame presents
            // immediately; the rest continues in WM_APP_LAYOUT_CHUNK slices
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                postWindowMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        }
    }

    // The latching rule, and its comment, moved to src/scrollbar.cpp so the Qt window
    // gets the same non-blinking right edge instead of a second copy of the rule.
    updateScrollbarExtent(app);

    // Sync preview scroll to editor scroll position using source-offset anchors
    if (editorPreviewVisible(app) && !app.scrollAnchors.empty() &&
        !app.editorLineByteOffsets.empty()) {
        // Find the editor's top visible line (row-aware in wrap mode)
        int topLine = (int)editorTopVisibleLine(app);
        topLine = std::max(0, std::min(topLine, (int)app.editorLineByteOffsets.size() - 1));
        size_t topByteOffset = app.editorLineByteOffsets[topLine];

        // Binary search for the anchor just before this byte offset
        size_t lo = 0, hi = app.scrollAnchors.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (app.scrollAnchors[mid].sourceOffset <= topByteOffset) lo = mid;
            else hi = mid;
        }

        // Interpolate between anchor[lo] and anchor[lo+1]
        float targetY;
        if (lo + 1 < app.scrollAnchors.size() &&
            app.scrollAnchors[lo + 1].sourceOffset > app.scrollAnchors[lo].sourceOffset) {
            float t = (float)(topByteOffset - app.scrollAnchors[lo].sourceOffset) /
                      (float)(app.scrollAnchors[lo + 1].sourceOffset - app.scrollAnchors[lo].sourceOffset);
            t = std::max(0.0f, std::min(t, 1.0f));
            targetY = app.scrollAnchors[lo].renderedY +
                       t * (app.scrollAnchors[lo + 1].renderedY - app.scrollAnchors[lo].renderedY);
        } else {
            // Last anchor or single anchor — use ratio for remaining content
            targetY = app.scrollAnchors[lo].renderedY;
            if (app.contentHeight > app.scrollAnchors[lo].renderedY) {
                size_t lastOffset = app.scrollAnchors[lo].sourceOffset;
                size_t totalBytes = app.editorLineByteOffsets.back();
                if (totalBytes > lastOffset) {
                    float t = (float)(topByteOffset - lastOffset) / (float)(totalBytes - lastOffset);
                    t = std::max(0.0f, std::min(t, 1.0f));
                    targetY += t * (app.contentHeight - app.scrollAnchors[lo].renderedY);
                }
            }
        }

        // The page clips at the sheet's bottom edge, so its scroll range
        // extends past the plain window-height clamp — the last blocks
        // must clear the sheet bottom with a little breathing room
        float previewMaxScroll =
            std::max(0.0f, app.contentHeight + dpi(app, 18.0f) -
                               editSheetRect(app).bottom);
        // renderedY includes the sheet's top padding; subtract it so an
        // editor at its top means a sheet at its top, and snap the last
        // half-line so the page top is always reachable (t11 feedback)
        float alignY = targetY - editSheetRect(app).top - dpi(app, 18.0f);
        float synced = std::max(0.0f, std::min(alignY, previewMaxScroll));
        if (app.editorScrollY <= 0.5f) synced = 0.0f;
        float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
        if (app.editorScrollY >= editorMax - 1.0f) {
            // The editor has bottomed out, but rendered content is taller
            // than its source (images, diagrams): let the preview overshoot
            // to reach its tail (#85). Scrolling the editor up re-syncs.
            app.scrollY = std::min(std::max(app.scrollY, synced), previewMaxScroll);
        } else {
            app.scrollY = synced;
        }
        app.targetScrollY = app.scrollY;
    }

    // Edit mode: split view rendering — everything sits on the desk
    // surface, the render sheet floats above it (design 10a)
    if (app.editMode) {
        app.startPageShowing = false;  // recents reload on the way back
        app.renderTarget->Clear(editDeskColor(app));

        float editorWidth = editorPaneWidth(app);
        float previewX = documentViewportX(app);
        float previewWidth = documentViewportWidth(app);

        // Render editor (left pane; full width when the preview is hidden)
        renderEditor(app, editorWidth);

        // The floating sheet (design 10a): desk, shadow, and sheet
        // surface first, then the document clips into the sheet
        renderEditSheetChrome(app);
        {
            tinta::RectF sheet = editSheetRect(app);
            app.renderTarget->PushAxisAlignedClip(
                sheet, tinta::AntialiasMode::Aliased);
        }

        tinta::Matrix3x2F originalTransform;
        app.renderTarget->GetTransform(&originalTransform);
        app.renderTarget->SetTransform(
            tinta::matrixTranslation(previewX, 0) * originalTransform);

        // The caret block's accent wash under the document content
        renderPreviewCaretBlock(app, previewWidth);

        goto render_document;
    }

    // Clear background
    app.renderTarget->Clear(app.theme.background);
    app.drawCalls++;

    // Folder browser open: the document shifts right in sync with the
    // panel's slide so it stays fully readable beside it
    if (documentViewportX(app) > 0.0f) {
        app.renderTarget->SetTransform(
            tinta::matrixTranslation(documentViewportX(app), 0));
    }

    // Start page: the launcher draws over the (empty) document; panels,
    // chips and overlays keep layering above it. Its recents reload on
    // every appearance so the list is always current.
    if (startPageActive(app)) {
        if (!app.startPageShowing) {
            app.startPageShowing = true;
            startPageRefreshRecents(app);
        }
        renderStartPage(app);
    } else {
        app.startPageShowing = false;
    }

render_document:

    // THE DOCUMENT PAINT PASS, WHICH USED TO BE 332 LINES OF THIS FUNCTION.
    //
    // It is src/paint_document.cpp now, and it moved for one reason: a Qt window
    // needs it, and it was locked inside the least portable file in the tree. What it
    // draws did not change -- the body over there is character-for-character what ran
    // here, and if a pixel moves on Windows the extraction is wrong. That file's
    // header comment names the two spots that are still behind #ifdef _WIN32.
    const DocumentViewport documentViewport = prepareDocumentViewport(app);

    // THREE VALUES, NOT FOUR, AS OF THE SCROLLBAR EXTRACTION. This comment said
    // "documentWidth at 7 later sites, cullMargin at 4, viewportTop and viewportBottom at
    // 2 each" -- fifteen references. All seven documentWidth sites were the scrollbar
    // block, which is now src/scrollbar.cpp and takes the DocumentViewport itself, so the
    // alias had zero readers left and is gone rather than left as an unused local. Eight
    // references remain and the count is corrected here rather than in a new sentence
    // below it, because a stale figure a reader can check against the code is worse than
    // no figure.
    //
    // Aliasing the rest keeps this a relocation rather than a 15-site edit, and const
    // rather than float because nothing below assigns them -- measured, not assumed.
    //
    // maxScrollX and maxScrollY were deliberately NOT aliased, and that decision is now
    // moot in the best way: a grep said they leaked, both later occurrences were
    // inner-scope re-declarations inside the scrollbar code, and that code has left this
    // file entirely. The lesson it recorded -- a declaration matching the grep for its own
    // identifier looks exactly like a read -- is why the extraction above re-checked all
    // four of its own locals by scope before moving them.
    const float viewportTop = documentViewport.top;
    const float viewportBottom = documentViewport.bottom;
    const float cullMargin = documentViewport.cullMargin;

    paintDocumentLayout(app, documentViewport);

    // Render code block copy button on hover
    if (app.hoveredCodeBlock >= 0 && app.hoveredCodeBlock < (int)app.codeBlocks.size()) {
        const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
        if (cb.bounds.bottom >= viewportTop - cullMargin &&
            cb.bounds.top <= viewportBottom + cullMargin) {
            float btnW = dpi(app, 72.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            // Blocks wider than the viewport keep their buttons reachable
            float rightEdge = std::min(
                cb.bounds.right, app.scrollX + documentViewportWidth(app));
            float btnX = rightEdge - btnW - btnPad - app.scrollX;
            float btnY = cb.bounds.top + btnPad - app.scrollY;

            // Button background
            app.brush->SetColor(tinta::colorF(
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                0.9f));
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(tinta::rectF(btnX, btnY, btnX + btnW, btnY + btnH), 4, 4),
                app.brush);

            // Localized copy label centered in the button
            app.brush->SetColor(tinta::colorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            const wchar_t* copyLabel = tr(app, "codeblock.copy");
            std::unique_ptr<tinta::TextLayout> btnLayout = app.textFactory->CreateTextLayout(
                copyLabel, (uint32_t)wcslen(copyLabel), app.codeFormat, btnW, btnH);
            if (btnLayout) {
                btnLayout->SetTextAlignment(tinta::TextAlignment::Center);
                btnLayout->SetParagraphAlignment(tinta::ParagraphAlignment::Center);
                app.renderTarget->DrawTextLayout(
                    tinta::pointF(btnX, btnY), btnLayout.get(), app.brush);
            }

            // Diagrams add a copy-as-image button beside the source copy
            if (cb.isDiagram) {
                float pngW = dpi(app, 52.0f);
                float pngX = btnX - pngW - dpi(app, 6.0f);

                // Oversized diagrams add the fit-to-width toggle
                if (cb.fitCandidate) {
                    float fitW = dpi(app, 44.0f);
                    float fitX = pngX - fitW - dpi(app, 6.0f);
                    app.brush->SetColor(tinta::colorF(
                        app.theme.isDark ? 0.3f : 0.85f,
                        app.theme.isDark ? 0.3f : 0.85f,
                        app.theme.isDark ? 0.3f : 0.85f,
                        0.9f));
                    app.renderTarget->FillRoundedRectangle(
                        tinta::roundedRect(
                            tinta::rectF(fitX, btnY, fitX + fitW,
                                        btnY + btnH), 4, 4),
                        app.brush);
                    app.brush->SetColor(tinta::colorF(
                        app.theme.isDark ? 0.9f : 0.15f,
                        app.theme.isDark ? 0.9f : 0.15f,
                        app.theme.isDark ? 0.9f : 0.15f,
                        1.0f));
                    const wchar_t* fitLabel =
                        cb.fitActive ? L"1:1" : L"Fit";
                    std::unique_ptr<tinta::TextLayout> fitLayout =
                        app.textFactory->CreateTextLayout(
                            fitLabel, (uint32_t)wcslen(fitLabel), app.codeFormat,
                            fitW, btnH);
                    if (fitLayout) {
                        fitLayout->SetTextAlignment(tinta::TextAlignment::Center);
                        fitLayout->SetParagraphAlignment(
                            tinta::ParagraphAlignment::Center);
                        app.renderTarget->DrawTextLayout(
                            tinta::pointF(fitX, btnY), fitLayout.get(), app.brush);
                    }
                }
                app.brush->SetColor(tinta::colorF(
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    0.9f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(
                        tinta::rectF(pngX, btnY, pngX + pngW, btnY + btnH), 4, 4),
                    app.brush);
                app.brush->SetColor(tinta::colorF(
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    1.0f));
                const wchar_t* pngLabel = tr(app, "diagram.png");
                std::unique_ptr<tinta::TextLayout> pngLayout =
                    app.textFactory->CreateTextLayout(
                        pngLabel, (uint32_t)wcslen(pngLabel), app.codeFormat, pngW, btnH);
                if (pngLayout) {
                    pngLayout->SetTextAlignment(tinta::TextAlignment::Center);
                    pngLayout->SetParagraphAlignment(
                        tinta::ParagraphAlignment::Center);
                    app.renderTarget->DrawTextLayout(
                        tinta::pointF(pngX, btnY), pngLayout.get(), app.brush);
                }
            }
            app.drawCalls++;
        }
    }

    // Table copy-as-TSV button on hover (same pattern as code blocks;
    // wide tables clamp the button into the visible viewport)
    if (app.hoveredTable >= 0 && app.hoveredTable < (int)app.tableRects.size()) {
        const auto& tb = app.tableRects[app.hoveredTable];
        if (tb.bounds.bottom >= viewportTop - cullMargin &&
            tb.bounds.top <= viewportBottom + cullMargin) {
            float btnW = dpi(app, 88.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            float rightEdge = std::min(
                tb.bounds.right, app.scrollX + documentViewportWidth(app));
            float btnX = rightEdge - btnW - btnPad - app.scrollX;
            float btnY = tb.bounds.top + btnPad - app.scrollY;

            app.brush->SetColor(tinta::colorF(
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                0.9f));
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(
                    tinta::rectF(btnX, btnY, btnX + btnW, btnY + btnH), 4, 4),
                app.brush);

            app.brush->SetColor(tinta::colorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            const wchar_t* copyLabel = tr(app, "table.copy");
            std::unique_ptr<tinta::TextLayout> btnLayout =
                app.textFactory->CreateTextLayout(
                    copyLabel, (uint32_t)wcslen(copyLabel), app.codeFormat, btnW, btnH);
            if (btnLayout) {
                btnLayout->SetTextAlignment(tinta::TextAlignment::Center);
                btnLayout->SetParagraphAlignment(
                    tinta::ParagraphAlignment::Center);
                app.renderTarget->DrawTextLayout(
                    tinta::pointF(btnX, btnY), btnLayout.get(), app.brush);
            }

            // Oversized tables add the fit-to-width toggle
            if (tb.fitCandidate) {
                float fitW = dpi(app, 44.0f);
                float fitX = btnX - fitW - dpi(app, 6.0f);
                app.brush->SetColor(tinta::colorF(
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    0.9f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(
                        tinta::rectF(fitX, btnY, fitX + fitW, btnY + btnH),
                        4, 4),
                    app.brush);
                app.brush->SetColor(tinta::colorF(
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    1.0f));
                const wchar_t* fitLabel = tb.fitActive ? L"1:1" : L"Fit";
                std::unique_ptr<tinta::TextLayout> fitLayout =
                    app.textFactory->CreateTextLayout(
                        fitLabel, (uint32_t)wcslen(fitLabel), app.codeFormat,
                        fitW, btnH);
                if (fitLayout) {
                    fitLayout->SetTextAlignment(tinta::TextAlignment::Center);
                    fitLayout->SetParagraphAlignment(
                        tinta::ParagraphAlignment::Center);
                    app.renderTarget->DrawTextLayout(
                        tinta::pointF(fitX, btnY), fitLayout.get(), app.brush);
                }
            }
            app.drawCalls++;
        }
    }

    // THE 95 LINES THAT DREW BOTH SCROLLBARS ARE NOW src/scrollbar.cpp, and the four
    // locals they used -- needsVScroll, needsHScroll, scrollbarSize, sbColorValue -- went
    // with them because a grep proved none of them is read below this point. That check is
    // the one paint_document.h's DocumentViewport comment records getting wrong the first
    // time, so it was done by name AND by scope: maxScrollX and maxScrollY inside the
    // moved block were inner-scope redeclarations shadowing the outer names, which is why
    // they are not parameters.
    paintDocumentScrollbars(app, documentViewport);

    // Draw selection highlights from the exact [selAnchor, selFocus) glyph
    // ranges. The copied text comes from the same range, so highlight and
    // clipboard always agree.
    if ((app.selecting || app.hasSelection) && !app.textRects.empty() &&
        app.selAnchor != app.selFocus) {
        size_t selA = std::min(app.selAnchor, app.selFocus);
        size_t selB = std::max(app.selAnchor, app.selFocus);

        std::vector<tinta::RectF> bars;
        selectionHighlightRects(app, selA, selB, bars);

        app.brush->SetColor(tinta::colorF(0.2f, 0.4f, 0.9f, 0.35f));
        float viewTop = app.scrollY - 50.0f;
        float viewBottom = app.scrollY + app.height + 50.0f;
        for (const auto& bar : bars) {
            if (bar.bottom < viewTop || bar.top > viewBottom) continue;
            app.renderTarget->FillRectangle(
                tinta::rectF(bar.left - app.scrollX, bar.top - app.scrollY,
                            bar.right - app.scrollX, bar.bottom - app.scrollY),
                app.brush);
            app.drawCalls++;
        }
    }

    // Draw search match highlights (search live through visible textRects)
    if (app.showSearch && !app.searchQuery.empty() && !app.textRects.empty() && !app.searchMatches.empty()) {
        // Collect visible match rects by intersecting search matches with text rects
        struct VisibleMatch {
            tinta::RectF rect;
            size_t matchIndex;
        };
        std::vector<VisibleMatch> visibleMatches;

        size_t matchIndex = 0;
        for (const auto& tr : app.textRects) {
            size_t textLen = tr.docLength;
            if (textLen == 0) continue;

            size_t rectStart = tr.docStart;
            size_t rectEnd = rectStart + textLen;

            // Advance to first match that could overlap this rect
            while (matchIndex < app.searchMatches.size()) {
                const auto& m = app.searchMatches[matchIndex];
                size_t mEnd = m.startPos + m.length;
                if (mEnd <= rectStart) {
                    matchIndex++;
                    continue;
                }
                break;
            }

            size_t mi = matchIndex;
            while (mi < app.searchMatches.size()) {
                const auto& m = app.searchMatches[mi];
                if (m.startPos >= rectEnd) break;

                size_t mStart = m.startPos;
                size_t mEnd = m.startPos + m.length;
                size_t overlapStart = std::max(rectStart, mStart);
                size_t overlapEnd = std::min(rectEnd, mEnd);

                if (overlapStart < overlapEnd) {
                    // Glyph-precise via the run layout (shared with text
                    // selection); falls back to interpolation for code
                    tinta::RectF highlightRect;
                    if (selectionRangeRect(app, tr, overlapStart, overlapEnd,
                                           highlightRect)) {
                        // Extend slightly for better visibility
                        highlightRect.left -= 1;
                        highlightRect.right += 1;
                        visibleMatches.push_back({highlightRect, mi});
                    }
                }

                if (mEnd <= rectEnd) {
                    mi++;
                } else {
                    break;  // Match spans beyond this rect; continue on next rect
                }
            }

            matchIndex = mi;
        }

        // Draw all matches - orange if it's the current match, yellow otherwise
        for (const auto& vm : visibleMatches) {
            bool isCurrent = (app.searchCurrentIndex >= 0 &&
                              vm.matchIndex == (size_t)app.searchCurrentIndex);

            if (isCurrent) {
                app.brush->SetColor(tinta::colorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
            } else {
                app.brush->SetColor(tinta::colorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
            }

            app.renderTarget->FillRectangle(
                tinta::rectF(vm.rect.left - app.scrollX, vm.rect.top - app.scrollY,
                            vm.rect.right - app.scrollX, vm.rect.bottom - app.scrollY),
                app.brush);
            app.drawCalls++;
        }
    }

    // Back to screen coordinates: notifications, stats, and overlays
    // (including the folder browser panel itself) are not shifted
    if (!app.editMode && documentViewportX(app) > 0.0f) {
        app.renderTarget->SetTransform(tinta::matrixIdentity());
    }

    // Review annotations: text tint, marker rail, hover preview (#126)
    renderAnnotations(app);

    // Link peek popup (dwell over a local .md link)
    renderLinkPeek(app);

    if (app.showStats) {
        wchar_t stats[768];
        wchar_t statsLine1[256];
        wchar_t statsLine2[512];
        swprintf_s(statsLine1, _countof(statsLine1), tr(app, "stats.line1"),
            app.parseTimeUs,
            app.layoutTimeUs,
            app.drawCalls);
        swprintf_s(statsLine2, _countof(statsLine2), tr(app, "stats.line2"),
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);
        swprintf_s(stats, _countof(stats), L"Tinta v%d.%d.%d  |  %ls\n%ls",
                   TINTA_VERSION_MAJOR, TINTA_VERSION_MINOR,
                   TINTA_VERSION_PATCH, statsLine1, statsLine2);

        float statsWidth = dpi(app, 600.0f);
        float statsHeight = dpi(app, 50.0f);

        app.brush->SetColor(tinta::colorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            tinta::rectF(app.width - statsWidth - dpi(app, 10.0f), app.height - statsHeight - dpi(app, 10.0f),
                       app.width - dpi(app, 10.0f), app.height - dpi(app, 10.0f)),
            app.brush);

        app.brush->SetColor(tinta::colorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats),
            app.statsFormat ? app.statsFormat : app.codeFormat,
            tinta::rectF(app.width - statsWidth - dpi(app, 5.0f), app.height - statsHeight - dpi(app, 5.0f),
                       app.width - dpi(app, 15.0f), app.height - dpi(app, 15.0f)),
            app.brush);
    }

    // Render overlays (search overlay handled separately for edit mode)
    if (app.showSearch && !app.editMode) {
        renderSearchOverlay(app);
        renderFolderSearchResults(app);
    }
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    if (app.annotEditorOpen) renderAnnotationEditor(app);

    // Close edit mode split view clipping
    if (app.editMode) {
        tinta::Matrix3x2F identity = tinta::matrixIdentity();
        app.renderTarget->SetTransform(identity);
        app.renderTarget->PopAxisAlignedClip();

        // Quick-note empty state: Open button in the blank preview pane
        renderQuickNoteEmptyState(app);

        // In-place table editing: cell input + hover affordances (#148)
        renderTableEditOverlay(app);

        // Render search overlay in screen coordinates (over editor pane)
        if (app.showSearch) renderSearchOverlay(app);
    }

    // Signal chips: every notification, one bottom-right system (t13).
    // Drawn after the edit-mode clip pops and above the side panels, so
    // chips reach the corner in both modes and over a pinned Contents
    renderSignalChips(app);

    // Title-bar tab strip: the caption itself, above every panel
    renderTabStrip(app);

    // Edit-mode tool rail (design t8): covers the strip's left corner
    // and the old gutter column, so it draws above both
    if (app.editMode) {
        renderEditRail(app);
        renderEditCtxMenu(app);
    }
    if (app.showThemeChooser) renderThemeChooser(app);
    if (app.showHelp) renderHelpOverlay(app);
    if (app.showSettings) renderSettingsOverlay(app);
    if (app.showLightbox) renderLightbox(app);
    if (app.showThemeEditor) renderThemeEditor(app);
    if (app.showShortcutEditor) renderShortcutEditor(app);

    renderTabSwitcher(app);
    renderTabMenu(app);
    if (app.showContextMenu) renderContextMenu(app);

    // Unsaved-changes dialog above it all (also entered from tab closes)
    if (app.confirmExitPending) renderConfirmExitDialog(app);
    if (app.createRefPending) renderCreateRefDialog(app);

    app.nativeRenderTarget->EndDraw();
}

// --- Update check ---
//
// Portable builds ask GitHub for the latest release tag at most once a
// day, on a worker thread that starts ten seconds after startup. Store
// installs update through the Store and skip all of this. The result
// surfaces as a small dismissable chip in the corner, never a dialog.

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
            // Unsaved buffers (active or parked in tabs) get the dialog
            // before the window may close; tabs stay open so the session
            // save still remembers them
            if (app && app->confirmExitPending) {
                app->pendingWindowClose = true;  // dialog is already asking
                return 0;
            }
            if (app) {
                for (size_t i = 0; i < app->tabs.size(); i++) {
                    bool dirty = (int)i == app->activeTab
                                     ? (app->editMode && app->editorDirty)
                                     : (app->tabs[i].editMode &&
                                        app->tabs[i].editorDirty);
                    if (!dirty) continue;
                    if ((int)i != app->activeTab) {
                        tabActivate(*app, hwnd, (int)i);
                    }
                    app->pendingWindowClose = true;
                    app->confirmExitPending = true;
                    app->confirmExitOpenedAt = std::chrono::steady_clock::now();
                    requestRepaint(hwnd);
                    return 0;
                }
            }
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
            if (app && app->followSystemTheme && lParam &&
                wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
                int want = autoThemeIndex(*app);
                if (want != app->currentThemeIndex) {
                    applyTheme(*app, want);
                    persistThemeChoice(*app);
                }
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
            if (wParam == TIMER_LINK_PEEK && app) handleLinkPeekTimer(*app, hwnd);
            if (wParam == TIMER_FILE_WATCH && app) handleFileWatchTimer(*app, hwnd);
            if (wParam == 2 && app) editorReparse(*app); // TIMER_EDITOR_REPARSE
            if (wParam == TIMER_CURSOR_BLINK && app) {
                app->cursorBlinkOn = !app->cursorBlinkOn;
                requestRepaint(hwnd);
            }
            if (wParam == TIMER_NOTIFICATION && app) {
                // Only draining chips need repaints; prompts are static
                // until answered
                bool fading = signalsNeedTicks(*app);
                if (fading) {
                    requestRepaint(hwnd);
                } else {
                    disarmTimer(hwnd, TIMER_NOTIFICATION);
                }
            }
            if (wParam == TIMER_FOLDER_SEARCH && app) {
                disarmTimer(hwnd, TIMER_FOLDER_SEARCH);
                startFolderSearchScan(*app);
            }
            if (wParam == TIMER_UPDATE_CHECK && app) {
                disarmTimer(hwnd, TIMER_UPDATE_CHECK);
                std::thread(updateCheckWorker, hwnd).detach();
            }
            if (wParam == TIMER_DRAFT_SAVE && app) {
                draftsSweep(*app);
            }
            if (wParam == TIMER_SELECT_SCROLL && app) {
                handleSelectScrollTimer(*app, hwnd);
            }
            if (wParam == TIMER_IMAGE_REFLOW && app) {
                // One relayout for however many images arrived since armed
                disarmTimer(hwnd, TIMER_IMAGE_REFLOW);
                app->layoutDirty = true;
                requestRepaint(hwnd);
            }
            if (wParam == TIMER_ZOOM_APPLY && app) {
                if (app->zoomFactor != app->appliedZoomFactor) {
                    // More zoom ticks arrived since the last apply
                    updateTextFormats(*app);
                    requestRepaint(hwnd);
                } else {
                    disarmTimer(hwnd, TIMER_ZOOM_APPLY);
                    app->zoomApplyPending = false;
                }
            }
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
            // Continue an incomplete document layout in ~10ms slices, yielding
            // to input between slices
            if (app && !app->layoutDirty && !app->layoutComplete) {
                bool done = layoutDocumentContinue(*app, 10000);
                requestRepaint(hwnd);  // scrollbar grows as layout fills in
                if (!done) postWindowMessage(hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
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
            PostQuitMessage(0);
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

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"-l" || arg == L"--light") {
            lightMode = true;
        } else if (arg == L"-s" || arg == L"--stats") {
            app.showStats = true;
        } else if (arg == L"/register" || arg == L"--register") {
            forceRegister = true;
        } else if (arg == L"--cascade") {
            cascadeWindow = true;
        } else if (arg == L"--tabbed") {
            // Drag-out satellites keep the tab row even with one tab
            app.forceTabStrip = true;
        } else if (arg == L"--edit") {
            startInEditMode = true;
        } else if (arg == L"--new") {
            quickNote = true;
        } else if (arg == L"--printpages" && i + 1 < argc) {
            printPagesDir = argv[++i];
        } else if (arg == L"--dumpchrome" && i + 1 < argc) {
            dumpChromePath = argv[++i];
        } else if (arg == L"--exporthtml" && i + 1 < argc) {
            exportHtmlPath = argv[++i];
        } else if (arg == L"--exportdocx" && i + 1 < argc) {
            exportDocxPath = argv[++i];
        } else if (arg == L"--exportpdf" && i + 1 < argc) {
            exportPdfPath = argv[++i];
        } else if (arg == L"--pos" && i + 2 < argc) {
            // Drag-out spawn: place the new window at the drop point
            posX = _wtoi(argv[++i]);
            posY = _wtoi(argv[++i]);
            hasPos = true;
        } else if (arg[0] != L'-' && arg[0] != L'/') {
            inputFile = platform::toUtf8(arg);
        }
    }
    LocalFree(argv);

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
        app.currentThemeIndex = 0;  // Paper (first light theme)
        app.theme = THEMES[0];
        app.darkMode = false;
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

    // Validate the saved window position against the monitors that exist
    // NOW — a position saved on a since-disconnected screen (docked laptop,
    // unplugged external monitor) would otherwise restore off-screen (#25)
    int windowX = savedSettings.windowX;
    int windowY = savedSettings.windowY;
    if (windowX != CW_USEDEFAULT && windowY != CW_USEDEFAULT) {
        RECT saved = { windowX, windowY,
                       windowX + savedSettings.windowWidth,
                       windowY + savedSettings.windowHeight };
        HMONITOR monitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            // Fully off every live monitor: clamp into the nearest one
            monitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (monitor && GetMonitorInfoW(monitor, &mi)) {
                const RECT& wa = mi.rcWork;
                int width = std::min<int>(savedSettings.windowWidth, wa.right - wa.left);
                int height = std::min<int>(savedSettings.windowHeight, wa.bottom - wa.top);
                windowX = std::max<int>(wa.left, std::min<int>(windowX, wa.right - width));
                windowY = std::max<int>(wa.top, std::min<int>(windowY, wa.bottom - height));
            } else {
                windowX = CW_USEDEFAULT;
                windowY = CW_USEDEFAULT;
            }
        }
    }

    // A window spawned for a newly created document steps down-right from
    // the saved position so it does not cover the window that spawned it
    if (hasPos) {
        // Tab drag-out: appear under the drop point
        windowX = posX;
        windowY = posY;
    } else if (cascadeWindow && windowX != CW_USEDEFAULT && windowY != CW_USEDEFAULT) {
        windowX += 40;
        windowY += 40;
    }

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
    // long after startup has finished (10 s timer arms a worker thread)
    if (app.updateCheckEnabled && !isStorePackaged() &&
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

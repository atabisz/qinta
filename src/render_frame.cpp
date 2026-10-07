#include "render_frame.h"

#include "annotations.h"
#include "editor.h"
#include "editrail.h"
#include "i18n.h"
#include "overlays.h"
#include "paint_document.h"
#include "portable_str.h"
#include "render.h"
#include "scrollbar.h"
#include "search.h"
#include "selection.h"
#include "signals.h"
#include "startpage.h"
#include "tableedit.h"
#include "tabs.h"
#include "win32_api.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

// ONE FRAME, BOTH WINDOWS. This is the body of src/main_d2d.cpp's render(), moved here on
// 2026-10-06 so the Qt window could draw it too. Until then qinta's paintEvent drew the document
// and its scrollbars and nothing else, so the editor, the start page, the tab strip, search,
// the panels, every overlay and both dialogs existed only on the Direct2D stack -- while the key
// and mouse routers were already driving all of them on Qt, invisibly.
//
// THE MOVE WAS VERBATIM apart from two things, and both are stated so a reader can check them.
// The frame bracket stayed behind: tinta's render() still calls BeginDraw, this, and EndDraw,
// and QtAppWindow::paintEvent supplies the QPainter through QtScopedRenderTarget. And the stats
// block's three swprintf_s(buf, _countof(buf), ...) became tinta::swprintf(buf, std::size(buf),
// ...), the portable spelling the rest of the tree already uses. tinta's window was captured
// before and after the move and hashed below its title bar, and the hashes are equal.
//
// It asks app.renderTarget to be the target being drawn: the caller begins the frame and ends
// it. Layout that does not finish inside the frame continues in WM_APP_LAYOUT_CHUNK messages,
// which both windows now route to src/input.cpp's handlePostedMessage.

void renderFrame(App& app) {
    if (!app.renderTarget) return;

    app.drawCalls = 0;

    // Print preview replaces the whole frame: the document is in print
    // layout while it is open, so the normal paths would draw nonsense
    if (app.showPrintPreview) {
        renderPrintPreview(app);
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
        tinta::swprintf(statsLine1, std::size(statsLine1), tr(app, "stats.line1"),
            app.parseTimeUs,
            app.layoutTimeUs,
            app.drawCalls);
        tinta::swprintf(statsLine2, std::size(statsLine2), tr(app, "stats.line2"),
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);
        tinta::swprintf(stats, std::size(stats), L"Qinta v%d.%d.%d  |  %ls\n%ls",
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
}

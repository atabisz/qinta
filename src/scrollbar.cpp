#include "scrollbar.h"

#include <algorithm>

#include "app.h"
#include "paint_document.h"

void updateScrollbarExtent(App& app) {
    // Incremental layout grows contentHeight in small steps. Keep scrollbar
    // visibility latched for the duration of that layout and only let its
    // extent grow, so the right edge cannot blink as chunks arrive.
    if (app.layoutComplete) {
        app.scrollbarContentHeight = app.contentHeight;
        app.verticalScrollbarVisible = app.contentHeight > app.height;
    } else if (app.contentHeight > app.height) {
        app.verticalScrollbarVisible = true;
        app.scrollbarContentHeight = std::max(app.scrollbarContentHeight,
                                              app.contentHeight);
    }
}

void paintDocumentScrollbars(App& app, const DocumentViewport& viewport) {
    // Both backends have a window in which these are null -- Direct2D between a device
    // loss and the rebuild, Qt outside a paint event -- and the Windows caller was
    // already inside a block that had checked, so this guard is new and is the only line
    // in this file that was not moved.
    if (!app.renderTarget || !app.brush) return;

    // The name the moved code used. paint_document.h's DocumentViewport comment counts
    // this as one of the four values that "leak past the end of the drawn range into the
    // chrome that main_d2d.cpp still draws itself (scrollbars, hover buttons, the table
    // edit rail)" -- so this file is that sentence getting one item shorter, and the
    // comment over there is corrected accordingly.
    const float documentWidth = viewport.documentWidth;

    // Determine scrollbar visibility
    bool needsVScroll = app.verticalScrollbarVisible;
    bool needsHScroll = app.contentWidth > documentWidth;
    float scrollbarSize = dpi(app, 14.0f);

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float scrollExtent = std::max((float)app.height, app.scrollbarContentHeight);
        float maxScrollY = std::max(0.0f, scrollExtent - app.height);
        float trackTop = chromeTopHeight(app);
        float trackHeight =
            app.height - trackTop - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / scrollExtent * trackHeight;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = trackTop +
            ((maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0);

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(tinta::colorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(tinta::rectF(documentWidth - sbWidth - dpi(app, 4.0f), sbY,
                                          documentWidth - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;

        // Find-match ticks along the track: the document's match silhouette
        // at a glance (overview-ruler pattern). Dense results thin out so a
        // one-letter query cannot flood the frame with fills.
        if (app.showSearch && !app.searchQuery.empty() &&
            !app.searchMatches.empty() && !app.editMode) {
            size_t step =
                std::max<size_t>(1, app.searchMatches.size() / 400);
            float tickLeft = documentWidth - dpi(app, 12.0f);
            float tickRight = documentWidth - dpi(app, 2.0f);
            auto tickY = [&](const App::SearchMatch& m, float& ty) {
                float docY;
                if (m.highlightRect.bottom > m.highlightRect.top) {
                    docY = m.highlightRect.top;
                } else if (!app.docText.empty()) {
                    docY = (float)m.startPos / (float)app.docText.size() *
                           scrollExtent;
                } else {
                    return false;
                }
                ty = trackTop + docY / scrollExtent * trackHeight;
                ty = std::min(ty, trackTop + trackHeight - dpi(app, 2.0f));
                return true;
            };
            app.brush->SetColor(tinta::colorF(1.0f, 0.82f, 0.0f, 0.55f));
            for (size_t i = 0; i < app.searchMatches.size(); i += step) {
                float ty;
                if (!tickY(app.searchMatches[i], ty)) continue;
                app.renderTarget->FillRectangle(
                    tinta::rectF(tickLeft, ty, tickRight, ty + dpi(app, 2.0f)),
                    app.brush);
            }
            // The current match always gets its tick, stepping or not
            if (app.searchCurrentIndex >= 0 &&
                app.searchCurrentIndex < (int)app.searchMatches.size()) {
                float ty;
                if (tickY(app.searchMatches[app.searchCurrentIndex], ty)) {
                    app.brush->SetColor(tinta::colorF(1.0f, 0.6f, 0.0f, 0.95f));
                    app.renderTarget->FillRectangle(
                        tinta::rectF(tickLeft, ty - dpi(app, 0.5f), tickRight,
                                    ty + dpi(app, 2.5f)),
                        app.brush);
                }
            }
            app.drawCalls++;
        }
    }

    // Draw horizontal scrollbar
    if (needsHScroll) {
        float maxScrollX = std::max(0.0f, app.contentWidth - documentWidth);
        float trackWidth = documentWidth - (needsVScroll ? scrollbarSize : 0);
        float sbWidth = trackWidth / app.contentWidth * trackWidth;
        sbWidth = std::max(sbWidth, dpi(app, 30.0f));
        float sbX = (maxScrollX > 0) ? (app.scrollX / maxScrollX * (trackWidth - sbWidth)) : 0;

        float sbHeight = (app.hScrollbarHovered || app.hScrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.hScrollbarHovered || app.hScrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(tinta::colorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(tinta::rectF(sbX, app.height - sbHeight - dpi(app, 4.0f),
                                          sbX + sbWidth, app.height - dpi(app, 4.0f)), 3, 3),
            app.brush);
        app.drawCalls++;
    }
}

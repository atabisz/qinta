#include "scroll.h"

#include <algorithm>

#include "app.h"

namespace tinta {

void clampDocumentScroll(App& app) {
    const float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    const float maxScrollX = std::max(0.0f, app.contentWidth - documentViewportWidth(app));

    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScrollY));
    app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));

    // input.cpp's scroll sites all end with this assignment. The two members exist for
    // a smooth-scroll animation that is not switched on anywhere.
    app.scrollY = app.targetScrollY;
    app.scrollX = app.targetScrollX;
}

}  // namespace tinta

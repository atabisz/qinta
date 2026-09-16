#include "scroll.h"

#include <algorithm>

#include "app.h"
#include "render.h"  // ensureLayoutComplete, for the End key only

namespace tinta {
namespace {

// The two steps, in device-independent pixels, from src/input.cpp. Named rather
// than inlined so a reader can see that the vertical wheel step (60) and the
// vertical KEY step (50) really are different numbers on purpose -- I checked,
// twice, because a 60 in both places looks more like the intended design than
// what the product actually does.
constexpr float kWheelStepDip = 60.0f;
constexpr float kLineStepDip = 50.0f;
constexpr float kHorizontalStepDip = 60.0f;
constexpr float kPageFraction = 0.8f;

}  // namespace

void clampDocumentScroll(App& app) {
    // BOTH MAXIMA ARE ZERO UNTIL layoutDocument HAS RUN, because they are
    // derived from app.contentHeight and app.contentWidth. That makes every
    // scroll before the first paint a no-op that lands at the top-left, which is
    // the correct outcome and not the one a reader expects, so qt_entry_tests
    // asserts it rather than this comment being the only record of it.
    const float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    const float maxScrollX = std::max(0.0f, app.contentWidth - documentViewportWidth(app));

    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScrollY));
    app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));

    // The Windows sites all end with this assignment. The two members exist for
    // a smooth-scroll animation that is not switched on anywhere.
    app.scrollY = app.targetScrollY;
    app.scrollX = app.targetScrollX;
}

void scrollDocumentByNotches(App& app, float notches) {
    // MINUS, because a positive notch is the wheel rolling AWAY from the user,
    // which moves the document's top edge down and the scroll offset toward zero.
    app.targetScrollY -= notches * dpi(app, kWheelStepDip);
    clampDocumentScroll(app);
}

void scrollDocumentHorizontallyByNotches(App& app, float notches) {
    app.targetScrollX -= notches * dpi(app, kHorizontalStepDip);
    clampDocumentScroll(app);
}

void scrollDocumentByStep(App& app, int direction) {
    if (direction == 0) return;
    const float sign = direction > 0 ? 1.0f : -1.0f;
    app.targetScrollY += sign * dpi(app, kLineStepDip);
    clampDocumentScroll(app);
}

void scrollDocumentHorizontallyByStep(App& app, int direction) {
    if (direction == 0) return;
    const float sign = direction > 0 ? 1.0f : -1.0f;
    app.targetScrollX += sign * dpi(app, kHorizontalStepDip);
    clampDocumentScroll(app);
}

void scrollDocumentByPage(App& app, int direction) {
    if (direction == 0) return;
    const float sign = direction > 0 ? 1.0f : -1.0f;
    // app.height, NOT the content height: a page is a screenful.
    app.targetScrollY += sign * app.height * kPageFraction;
    clampDocumentScroll(app);
}

void scrollDocumentToTop(App& app) {
    app.targetScrollY = 0.0f;
    clampDocumentScroll(app);
}

void scrollDocumentToBottom(App& app) {
    ensureLayoutComplete(app);
    // Deliberately an out-of-range value rather than the computed maximum: the
    // clamp owns that arithmetic, and computing it here a second time is how the
    // two copies drift apart.
    app.targetScrollY = app.contentHeight;
    clampDocumentScroll(app);
}

}  // namespace tinta

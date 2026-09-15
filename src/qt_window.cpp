#include "qt_window.h"

#include <QPaintEvent>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "app.h"
#include "qt_init.h"
#include "render_qt.h"
#include "text_api.h"

namespace {

// Deliberately not localised through tr(). The i18n table is keyed by the app's own
// string ids and this line is scaffolding that goes away when render.cpp compiles
// off Windows -- adding it to the catalogue would leave a translated string behind
// for a screen that will not exist.
const wchar_t kPlaceholder[] =
    L"tinta - Qt shell. The document renderer is not on this stack yet.";

// One less than the array extent, which is the terminator. Computed rather than
// written: the seam takes an explicit length, and a hand-typed count on a string
// somebody will reword is a defect waiting for the reword.
constexpr uint32_t kPlaceholderLength =
    static_cast<uint32_t>(sizeof(kPlaceholder) / sizeof(kPlaceholder[0]) - 1);

// Matches the body size the start page uses. Not read from the theme, because
// D2DTheme carries font FAMILIES and no sizes -- every size in this tree is a
// literal at its call site.
constexpr float kPlaceholderFontSize = 18.0f;

// The half-height of the band the line is centred in. Generous, because the test
// samples pixels inside it and a band tight to the glyph box would make the test
// depend on whichever font the platform substituted.
constexpr float kPlaceholderBandHalfHeight = 40.0f;

}  // namespace

QtAppWindow::QtAppWindow(App& app, QWidget* parent) : QWidget(parent), app_(app) {}

tinta::RectF QtAppWindow::placeholderRect() const {
    const float w = static_cast<float>(width());
    const float mid = static_cast<float>(height()) * 0.5f;
    // left, top, right, bottom -- RectF inherited Direct2D's shape, and getting that
    // wrong produces a plausible-looking rectangle rather than an error.
    return tinta::RectF{0.0f, mid - kPlaceholderBandHalfHeight, w,
                        mid + kPlaceholderBandHalfHeight};
}

void QtAppWindow::paintEvent(QPaintEvent* event) {
    (void)event;
    paintCount_++;

    // The QPainter lives exactly as long as this object, which is exactly as long as
    // this function. That is the whole reason QtScopedRenderTarget exists rather than
    // an install/release pair the way the text factory has.
    QtScopedRenderTarget target(app_, *this);
    renderTargetInstalled_ = target.target() != nullptr;
    if (!renderTargetInstalled_) return;

    // Every line below goes through app_.renderTarget, the pointer the 566 drawing
    // sites in the tree read. Nothing here names QtRenderTarget.
    app_.renderTarget->Clear(app_.theme.background);

    // No text factory means no text, and it means it QUIETLY. That is not an
    // oversight being tolerated: tests/qt_entry_tests.cpp releases the factory and
    // grabs again, and the band coming back as flat background is what makes the
    // text assertion in the run before it discriminating rather than decorative.
    if (!app_.textFactory) return;

    std::unique_ptr<tinta::TextFormat> format = app_.textFactory->CreateTextFormat(
        app_.theme.fontFamily, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
        tinta::FontStretch::Normal, kPlaceholderFontSize, L"en-us");
    if (!format) return;
    format->SetTextAlignment(tinta::TextAlignment::Center);
    format->SetParagraphAlignment(tinta::ParagraphAlignment::Center);

    // Constructed here rather than held as a member: a brush is one colour and the
    // theme can change under a running window, so caching one would be a cache with
    // an invalidation rule and nothing to gain.
    QtBrush brush(app_.theme.text);
    app_.renderTarget->DrawText(kPlaceholder, kPlaceholderLength, format.get(),
                                placeholderRect(), &brush);
}

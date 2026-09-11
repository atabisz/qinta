// The Qt harness for the shared drawing-seam assertions.
//
// The assertions themselves are in tests/render_api_shared.cpp and are run
// unchanged by tests/render_d2d_tests.cpp against Direct2D. That split is the
// point: two backends each with their own tests would only show that each is
// self-consistent, where the interesting failure is the two disagreeing. Nothing
// below asserts anything -- it only supplies a surface, a session and a pixel
// reader.

#include "render_api_shared.h"
#include "render_qt.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>

#include <cstdio>

namespace {

class QtHarness final : public RenderHarness {
public:
    void newSurface() override {
        target_.reset();  // a live QPainter on the old image must not outlive it
        // Format_ARGB32 rather than ARGB32_Premultiplied on purpose: a
        // premultiplied surface would make every colour assertion depend on the
        // alpha it was blended with, so a wrong colour and a wrong alpha could
        // cancel out. QImage::pixel() on ARGB32 returns straight alpha, which is
        // what Rgba means.
        image_ = QImage(kWidth, kHeight, QImage::Format_ARGB32);
        image_.fill(Qt::transparent);
    }

    tinta::RenderTarget& begin() override {
        target_ = std::make_unique<QtRenderTarget>(image_);
        return *target_;
    }

    void end() override { target_.reset(); }

    std::unique_ptr<tinta::Brush> makeBrush(const tinta::Color& color) override {
        return std::make_unique<QtBrush>(color);
    }

    std::unique_ptr<tinta::Bitmap> makeBitmap(int width, int height,
                                             const std::vector<Rgba>& pixels) override {
        if (width <= 0 || height <= 0 ||
            pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
            return nullptr;
        // Format_ARGB32 is STRAIGHT alpha, which is what Rgba means, so qRgba takes
        // the values through unchanged. The D2D harness has to premultiply here; that
        // asymmetry is the whole reason this factory is the harness's job.
        QImage image(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                const Rgba& p = pixels[static_cast<size_t>(y) * width + x];
                image.setPixel(x, y, qRgba(p.r, p.g, p.b, p.a));
            }
        return std::make_unique<QtBitmap>(std::move(image));
    }

    Rgba pixel(int x, int y) override {
        const QRgb p = image_.pixel(x, y);
        return Rgba{qRed(p), qGreen(p), qBlue(p), qAlpha(p)};
    }

    tinta::TextFactory& textFactory() override { return textFactory_; }

    // DejaVu Sans for the same reason tests/qt_text_tests.cpp picked it: it ships
    // with Qt's own fontconfig setup, so it resolves under the offscreen platform
    // without depending on what the host happens to have installed.
    const wchar_t* fontFamily() const override { return L"DejaVu Sans"; }

    const char* name() const override { return "qt"; }

private:
    QImage image_;
    std::unique_ptr<QtRenderTarget> target_;
    QtTextFactory textFactory_;
};

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // The same refusal qt_text_tests.cpp makes, and for the same measured reason:
    // under QT_QPA_PLATFORM=minimal the font database is EMPTY, every family request
    // resolves to nothing, and QTextLayout still returns plausible numbers and exits
    // 0. A text assertion run against that would be green while measuring nothing, so
    // a missing font database has to be a refusal (exit 2) and not a result.
    if (QFontDatabase::families().isEmpty()) {
        std::printf("no font families available -- refusing to report a result (this is NOT a "
                    "pass). Check QT_QPA_PLATFORM is 'offscreen' and not 'minimal'.\n");
        return 2;
    }

    QtHarness harness;
    return runRenderApiAssertions(harness) == 0 ? 0 : 1;
}

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

#include <QGuiApplication>
#include <QImage>

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

    Rgba pixel(int x, int y) override {
        const QRgb p = image_.pixel(x, y);
        return Rgba{qRed(p), qGreen(p), qBlue(p), qAlpha(p)};
    }

    const char* name() const override { return "qt"; }

private:
    QImage image_;
    std::unique_ptr<QtRenderTarget> target_;
};

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QtHarness harness;
    return runRenderApiAssertions(harness) == 0 ? 0 : 1;
}

// The Direct2D harness for the shared drawing-seam assertions.
//
// WHY THIS FILE IS THE POINT OF THE WHOLE SEAM
//
// include/render_api.h keeps Direct2D's method names and signatures and claims that
// doing so is what makes ~915 call sites move to another backend unedited. Until
// this file existed, the only implementation was Qt's -- so the interface had been
// shaped BY a non-Windows backend and never checked back AGAINST the one it was
// modelled on. An interface with one implementation is not an abstraction, it is
// indirection.
//
// It runs tests/render_api_shared.cpp unchanged, the same assertions in the same
// order as the Qt harness. A disagreement between the two backends -- a corner that
// is round under Qt and square under Direct2D, a clip that leaks on one side only, a
// colour that rounds differently -- fails the assertion naming that behaviour, on
// the backend that got it wrong, and the summary line says which backend that was.
//
// Rendering goes to a WIC bitmap rather than a window, using the same recipe as
// tests/math_layout_tests.cpp (software target, 32bppPBGRA) so this test needs no
// display and no message loop.

#include "render_api_shared.h"
#include "render_d2d.h"

#include <windows.h>

#include <d2d1helper.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

class D2DHarness final : public RenderHarness {
public:
    // Returns false rather than asserting: a machine that cannot create a software
    // Direct2D target has not failed the seam, it has failed to provide an
    // instrument, and those two must not report the same way. main() turns this into
    // exit 2, matching the refusal convention the other tests in this tree use.
    bool initialize() {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&wic_))))
            return false;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())))
            return false;
        if (FAILED(wic_->CreateBitmap(kWidth, kHeight, GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapCacheOnLoad, &bitmap_)))
            return false;
        const D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        if (FAILED(d2d_->CreateWicBitmapRenderTarget(bitmap_.Get(), props, &target_)))
            return false;
        wrapper_ = std::make_unique<D2DRenderTarget>(target_.Get());
        // DWRITE_FACTORY_TYPE_SHARED is what src/d2d_init.cpp uses, so the font cache
        // this test measures against is the same one the app would get.
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
            return false;
        textFactory_ = std::make_unique<D2DTextFactory>(dwrite_.Get());
        return true;
    }

    // The bitmap and the render target are created ONCE and reused, so a "new
    // surface" is a Clear to transparent. Qt's harness allocates a fresh QImage,
    // which comes out the same way because it fills it with transparent -- in both
    // cases the control that follows is checking the READBACK path reports nothing
    // painted, not that the allocator handed back zeroed memory. Reusing the target
    // also means a brush can never outlive the device that made it.
    void newSurface() override {
        target_->BeginDraw();
        target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        endDraw();
    }

    tinta::RenderTarget& begin() override {
        target_->BeginDraw();
        pixelsValid_ = false;
        return *wrapper_;
    }

    void end() override { endDraw(); }

    std::unique_ptr<tinta::Brush> makeBrush(const tinta::Color& color) override {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target_->CreateSolidColorBrush(color, &brush))) {
            std::printf("FAIL: CreateSolidColorBrush failed -- the harness cannot make a brush\n");
            brushFailures_++;
            return nullptr;
        }
        // A Direct2D brush is a device resource, so unlike QtBrush it cannot own
        // itself. The harness keeps every one alive for its whole run rather than
        // freeing them per surface: the count is bounded (one per assertion block),
        // and the alternative -- releasing on newSurface -- would leave a silently
        // dangling D2DBrush the first time an assertion held a brush across two
        // surfaces.
        ID2D1SolidColorBrush* raw = brush.Get();
        brushes_.push_back(std::move(brush));
        return std::make_unique<D2DBrush>(raw);
    }

    std::unique_ptr<tinta::Bitmap> makeBitmap(int width, int height,
                                             const std::vector<Rgba>& pixels) override {
        if (width <= 0 || height <= 0 ||
            pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
            return nullptr;

        // 32bppPBGRA to match the target, so: channel order reversed AND
        // PREMULTIPLIED. Rgba is straight alpha (the Qt harness hands its values
        // through untouched), so the multiply happens here -- the exact inverse of
        // what pixel() undoes on readback. Doing it in one place per backend is what
        // lets the shared assertions mean one thing by "half-transparent red".
        std::vector<BYTE> bgra(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < pixels.size(); i++) {
            const Rgba& p = pixels[i];
            bgra[i * 4 + 0] = static_cast<BYTE>(p.b * p.a / 255);
            bgra[i * 4 + 1] = static_cast<BYTE>(p.g * p.a / 255);
            bgra[i * 4 + 2] = static_cast<BYTE>(p.r * p.a / 255);
            bgra[i * 4 + 3] = static_cast<BYTE>(p.a);
        }

        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap> bitmap;
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width),
                                                    static_cast<UINT32>(height)),
                                         bgra.data(), static_cast<UINT32>(width) * 4, props,
                                         &bitmap))) {
            std::printf("FAIL: CreateBitmap failed -- the harness cannot make a bitmap\n");
            bitmapFailures_++;
            return nullptr;
        }
        // Held for the run for the same reason the brushes are: a device resource
        // cannot own itself, and releasing per surface would leave a dangling
        // D2DBitmap the first time an assertion held one across two surfaces.
        ID2D1Bitmap* raw = bitmap.Get();
        bitmaps_.push_back(std::move(bitmap));
        return std::make_unique<D2DBitmap>(raw);
    }

    Rgba pixel(int x, int y) override {
        if (!pixelsValid_) readBack();
        const size_t i = (static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)) * 4;
        if (i + 3 >= pixels_.size()) return Rgba{};
        // 32bppPBGRA: blue, green, red, alpha, and PREMULTIPLIED. Rgba means straight
        // alpha, which is what Qt's Format_ARGB32 reports, so undo the multiply here
        // rather than teaching the shared assertions about two alpha conventions.
        const int b = pixels_[i + 0];
        const int g = pixels_[i + 1];
        const int r = pixels_[i + 2];
        const int a = pixels_[i + 3];
        if (a == 0) return Rgba{0, 0, 0, 0};
        if (a == 255) return Rgba{r, g, b, a};
        return Rgba{unpremultiply(r, a), unpremultiply(g, a), unpremultiply(b, a), a};
    }

    tinta::TextFactory& textFactory() override { return *textFactory_; }

    // Segoe UI is the family src/d2d_init.cpp asks DirectWrite for, and it is present
    // on every supported Windows version. The Qt harness answers DejaVu Sans; the
    // shared assertions must not know which, which is why they ask.
    const wchar_t* fontFamily() const override { return L"Segoe UI"; }

    const char* name() const override { return "direct2d"; }

    int harnessFailures() const { return brushFailures_ + drawFailures_ + bitmapFailures_; }

private:
    static int unpremultiply(int channel, int alpha) {
        const int value = (channel * 255 + alpha / 2) / alpha;
        return value > 255 ? 255 : value;
    }

    void endDraw() {
        // EndDraw's HRESULT is the reason device lifetime is outside the portable
        // interface, and ignoring it here would let a lost device read as a clean
        // surface: every subsequent pixel would come back unpainted and every
        // "is NOT painted" assertion would pass.
        const HRESULT hr = target_->EndDraw();
        if (FAILED(hr)) {
            std::printf("FAIL: EndDraw failed with 0x%08lX\n", static_cast<unsigned long>(hr));
            drawFailures_++;
        }
        pixelsValid_ = false;
    }

    void readBack() {
        pixels_.assign(static_cast<size_t>(kWidth) * kHeight * 4, 0);
        const HRESULT hr = bitmap_->CopyPixels(nullptr, kWidth * 4,
                                               static_cast<UINT>(pixels_.size()), pixels_.data());
        if (FAILED(hr)) {
            std::printf("FAIL: CopyPixels failed with 0x%08lX\n", static_cast<unsigned long>(hr));
            drawFailures_++;
        }
        pixelsValid_ = true;
    }

    ComPtr<IWICImagingFactory> wic_;
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IWICBitmap> bitmap_;
    ComPtr<ID2D1RenderTarget> target_;
    ComPtr<IDWriteFactory> dwrite_;
    std::unique_ptr<D2DRenderTarget> wrapper_;
    std::unique_ptr<D2DTextFactory> textFactory_;
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushes_;
    std::vector<ComPtr<ID2D1Bitmap>> bitmaps_;
    std::vector<BYTE> pixels_;
    bool pixelsValid_ = false;
    int brushFailures_ = 0;
    int drawFailures_ = 0;
    int bitmapFailures_ = 0;
};

}  // namespace

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("cannot initialize COM -- refusing to report a result\n");
        return 2;
    }
    int result = 0;
    {
        D2DHarness harness;
        if (!harness.initialize()) {
            std::printf("cannot create a software Direct2D render target -- refusing to report "
                        "a result (this is NOT a pass)\n");
            result = 2;
        } else {
            const int failures = runRenderApiAssertions(harness);
            result = (failures == 0 && harness.harnessFailures() == 0) ? 0 : 1;
        }
    }
    // EVERY ComPtr in the harness must Release BEFORE this, which is why the refusal
    // above sets a result instead of returning. It returned early at first, calling
    // CoUninitialize with the harness still alive, and the destructor then released
    // COM objects into a torn-down apartment: the process SEGFAULTED (exit 139) and
    // the explanatory message above never reached stdout. Found by forcing
    // initialize() to fail -- the refusal path is exactly the path that only runs on
    // a machine where something is already wrong, so it never runs here otherwise.
    CoUninitialize();
    return result;
}

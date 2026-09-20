// The Windows harness for the shared PDF-seam assertions.
//
// It runs tests/pdf_export_shared.cpp unchanged, the same assertions in the same order as
// the Qt harness, against the arm that routes the job at the "Microsoft Print to PDF"
// driver. Until this file existed, include/pdf_export.h had one implementation and every
// portable property it claims was claimed by prose: an interface with one implementation
// is indirection, not abstraction, which is this tree's own sentence about the drawing
// seam (CMakeLists.txt) and is no less true of this one.
//
// WHY A DEVICE CONTEXT OFF A D3D DEVICE AND NOT A WIC BITMAP TARGET, which is what
// tests/render_d2d_tests.cpp uses. beginPdfDocument QueryInterfaces its target for
// ID2D1DeviceContext and asks that for its ID2D1Device, because the print control is
// created on the device. A render target from a plain ID2D1Factory is a 1.0 object with
// no device behind it at all, so the shortest path to something the seam accepts is the
// path the app itself takes: a D3D11 device, a D2D device on top, and a context off that.
// Nothing is ever drawn into this context -- XpsPageEmitter creates a second context off
// the same device for the recording -- so it needs no target bitmap, and that is a
// property of the seam rather than a shortcut.

#include "pdf_export_shared.h"
#include "render_d2d.h"

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

class Win32PdfHarness final : public PdfHarness {
public:
    // Returns false rather than asserting, for the reason tests/render_d2d_tests.cpp gives
    // and paid for: a machine that cannot create a Direct2D device has not failed the
    // seam, it has failed to supply an instrument, and the two must not report the same
    // way. main() turns a false into exit 2.
    bool initialize() {
        // Hardware first, WARP second. The app asks for hardware and this follows it, but
        // a CI or remote-session host with no usable adapter should refuse for the print
        // control's sake rather than for the adapter's, so the software fallback is tried
        // before giving up.
        const D3D_DRIVER_TYPE types[2] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
        for (D3D_DRIVER_TYPE type : types) {
            D3D_FEATURE_LEVEL level;
            ComPtr<ID3D11DeviceContext> d3dContext;
            if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                            D3D11_SDK_VERSION, d3d_.GetAddressOf(), &level,
                                            d3dContext.GetAddressOf()))) {
                driver_ = (type == D3D_DRIVER_TYPE_HARDWARE) ? "hardware" : "warp";
                break;
            }
            d3d_.Reset();
        }
        if (!d3d_) return false;

        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d_.As(&dxgi))) return false;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory1), nullptr,
                                     reinterpret_cast<void**>(factory_.GetAddressOf()))))
            return false;
        if (FAILED(factory_->CreateDevice(dxgi.Get(), device_.GetAddressOf()))) return false;
        if (FAILED(device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                context_.GetAddressOf())))
            return false;

        wrapper_ = std::make_unique<D2DRenderTarget>(context_.Get());

        // The scratch directory is the process's own temp directory, created rather than
        // assumed: the shared assertions have a CONTROL that it is writable, and a
        // harness that hands over a path nobody made would fail that control for a reason
        // that has nothing to do with the seam.
        std::error_code ec;
        scratch_ = std::filesystem::temp_directory_path(ec);
        if (ec) return false;
        return true;
    }

    const char* driver() const { return driver_; }

    tinta::RenderTarget& target() override { return *wrapper_; }

    std::wstring scratchDir() override { return scratch_.wstring(); }

    const char* name() const override { return "direct2d-xps"; }

private:
    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<ID2D1Device> device_;
    ComPtr<ID2D1DeviceContext> context_;
    std::unique_ptr<D2DRenderTarget> wrapper_;
    std::filesystem::path scratch_;
    const char* driver_ = "none";
};

}  // namespace

int main() {
    // APARTMENTTHREADED, matching tests/render_d2d_tests.cpp and the app: the seam calls
    // CoCreateInstance for the print-document target factory and for a WIC factory, and
    // src/pdf_export_win32.cpp says out loud that it depends on its caller having
    // initialised COM.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("cannot initialize COM -- refusing to report a result\n");
        return 2;
    }
    int result = 0;
    {
        Win32PdfHarness harness;
        if (!harness.initialize()) {
            std::printf("cannot create a Direct2D device context -- refusing to report a "
                        "result (this is NOT a pass)\n");
            result = 2;
        } else {
            std::printf("       d3d driver: %s\n", harness.driver());
            result = runPdfExportAssertions(harness) == 0 ? 0 : 1;
        }
    }
    // Every ComPtr above must Release BEFORE this line, which is why the refusal path sets
    // a result instead of returning: tests/render_d2d_tests.cpp took exit 139 and lost its
    // own explanatory message by getting this wrong, and the path that does it is the one
    // that only runs on a machine where something is already broken.
    CoUninitialize();
    return result;
}

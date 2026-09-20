#include "pdf_export.h"

// The Windows arm of the PDF seam: the job is routed to the "Microsoft Print to PDF"
// driver with its output redirected into a file stream, so the OS produces the PDF at
// the chosen path with no dialog. There is no PDF library in this tree and this is why
// there does not need to be one -- the print pipeline already emits vector pages, and
// font embedding including CJK is the driver's job. That last clause is the whole
// reason printDocument is a LATER slice than this one: a QPrinter port has to prove
// CJK embedding for itself, and nothing here does it for free.
//
// render_d2d.h arrives through xps_page_emitter.h and brings <windows.h> with it, in
// the order render_api.h requires (see its note on the DrawText macro).
#include "xps_page_emitter.h"

#include <documenttarget.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <cassert>

namespace tinta {
namespace {

// Everything the XPS route holds open for the life of one document, released in the
// reverse of the order the inline version released it in -- which is to say the same
// order, because the inline version released it by hand at the bottom of one function
// and this destructor reads as that tail.
class XpsPdfPageSink final : public PdfPageSink {
public:
    ~XpsPdfPageSink() override {
        // Explicit rather than left to member order, because the ORDER IS THE
        // BEHAVIOUR: the emitter's own device context went out of scope at the end of
        // the `if (printControl)` block in the code this replaces, i.e. before the
        // print control was released. Members would be destroyed last-declared-first
        // and happen to give the same sequence; saying it once here means a later
        // reordering of the declarations cannot silently change it.
        emitter_.reset();
        if (printControl_) printControl_->Release();
        if (target_) target_->Release();
        if (wic_) wic_->Release();
        if (stream_) {
            // Committed even for a document nobody finished, which is what the inline
            // version did -- Commit sat after the `if (printControl)` block and ran on
            // every path that got as far as creating the stream.
            stream_->Commit(STGC_DEFAULT);
            stream_->Release();
        }
    }

    bool open(ID2D1DeviceContext* sourceContext, const std::wstring& path,
              const std::wstring& documentTitle) {
        if (FAILED(SHCreateStreamOnFileEx(
                path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream_))) {
            stream_ = nullptr;
            return false;
        }

        IPrintDocumentPackageTargetFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(
                __uuidof(PrintDocumentPackageTargetFactory), nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            return false;
        }
        HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
            L"Microsoft Print to PDF", documentTitle.c_str(), stream_, nullptr,
            &target_);
        factory->Release();
        if (FAILED(hr) || !target_) {
            target_ = nullptr;
            return false;
        }

        // A WIC factory OF THIS SINK'S OWN, where the inline version spent
        // app.wicFactory. The seam cannot be handed one -- IWICImagingFactory in a
        // portable header is exactly what this header refuses -- and creating one is
        // the established answer: src/image_win32.cpp's ImagingScope does the same
        // CoCreateInstance per call, having found that caching a factory across
        // apartments was a live crash. No ScopedApartment here, though, and that is a
        // difference worth stating: both callers of this seam run on the main thread
        // with COM already initialised, which the CoCreateInstance above has always
        // depended on too.
        //
        // HELD FOR THE SINK'S LIFETIME rather than released after CreatePrintControl.
        // A print control that retains the factory is obliged to AddRef it, so the
        // shorter lifetime ought to be safe -- but app.wicFactory outlived every
        // document that ever used it, so the obligation has never been tested here,
        // and this is not the slice to start.
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) {
            wic_ = nullptr;
            return false;
        }

        ID2D1Device* device = nullptr;
        sourceContext->GetDevice(&device);
        if (!device) return false;
        device->CreatePrintControl(wic_, target_, nullptr, &printControl_);
        device->Release();
        if (!printControl_) return false;

        emitter_ = std::make_unique<XpsPageEmitter>(sourceContext, printControl_);
        return true;
    }

    bool addPage(float pageW, float pageH, const PagePainter& paint) override {
        return emitter_ && emitter_->addPage(pageW, pageH, paint);
    }

    bool finish() override {
        return printControl_ && SUCCEEDED(printControl_->Close());
    }

private:
    IStream* stream_ = nullptr;
    IPrintDocumentPackageTarget* target_ = nullptr;
    IWICImagingFactory* wic_ = nullptr;
    ID2D1PrintControl* printControl_ = nullptr;
    std::unique_ptr<XpsPageEmitter> emitter_;
};

}  // namespace

std::unique_ptr<PdfPageSink> beginPdfDocument(RenderTarget& target,
                                              const std::wstring& path,
                                              const std::wstring& documentTitle) {
    // Asserted, not tolerated -- the same shape as decodeImageFileToBitmap in
    // image_win32.cpp and for the same reason: nullptr out of this function is
    // indistinguishable from an unwritable path, so a Qt target arriving in the
    // Direct2D backend would be diagnosed as a full disk.
    assert(dynamic_cast<D2DRenderTarget*>(&target) != nullptr &&
           "a non-Direct2D RenderTarget reached the Direct2D PDF backend");
    auto* d2dTarget = dynamic_cast<D2DRenderTarget*>(&target);
    if (!d2dTarget || !d2dTarget->native()) return nullptr;

    // The device comes from the render target rather than from App, and the two are the
    // same object: d2d_init.cpp seats the wrapper on app.nativeRenderTarget and obtains
    // app.deviceContext by QueryInterface on that same native. So this QI reaches the
    // context the inline code used, and not a second one that happens to share a device.
    ID2D1DeviceContext* sourceContext = nullptr;
    if (FAILED(d2dTarget->native()->QueryInterface(IID_PPV_ARGS(&sourceContext))) ||
        !sourceContext) {
        return nullptr;
    }

    auto sink = std::make_unique<XpsPdfPageSink>();
    const bool ok = sink->open(sourceContext, path, documentTitle);
    // Released here, not held: XpsPageEmitter asked it for the device and created a
    // context of its own off that device, so nothing below retains this pointer.
    sourceContext->Release();
    if (!ok) return nullptr;  // ~XpsPdfPageSink still commits the stream; see the header
    return sink;
}

}  // namespace tinta

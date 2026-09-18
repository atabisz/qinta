#include "image_api.h"

#include "text_utils.h"

#include <windows.h>
#include <urlmon.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <d2d1.h>
#include <d2d1_3.h>

// After <windows.h>, and that ordering is load-bearing rather than stylistic:
// render_d2d.h pulls in render_api.h, which hard-errors unless the DrawText macro is
// already in scope. It is included at all for D2DRenderTarget and D2DOwnedBitmap, which
// decodeImageFileToBitmap needs at the bottom of this file. image_api.h itself stays
// clear of it and only forward-declares -- see the note there for why that matters.
#include "render_d2d.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>

// The Windows half of include/image_api.h, and the reason that header exists: these
// three includes -- urlmon.h, shlwapi.h and d2d1_3.h -- were sitting unguarded in
// render.cpp, where they made a 3,498-line file FATAL off Windows and therefore
// unmeasured for six spans. app.h already guards d2d1.h, dwrite.h and wincodec.h
// behind `#ifdef _WIN32`; these three had no such guard, so nothing else in the file
// could be read past them.
//
// Everything here is MOVED from render.cpp's asyncImageWorker and loadSvgBitmap, not
// rewritten. The two-pass SVG structure survives, including the pre-1703 fallback
// where QueryInterface for ID2D1DeviceContext5 fails and the caller shows alt text.
// The strict `< kMaxImageDimension` test is the original's `< 16384`, not a rounded
// `<=`. TWO differences from the original are deliberate and each is argued at its
// site below: the pixel copy, and where COM is initialized. There was a third -- the
// seam owning process-lifetime factories instead of creating them per call -- and it
// was a defect rather than a difference, so it is gone. ImagingScope says what it did.
namespace tinta {

namespace {

// A COM apartment for the calling thread, entered for the duration of one call.
//
// asyncImageWorker used to wrap its whole body in CoInitializeEx/CoUninitialize
// because it runs on a detached std::thread that has no apartment. That pairing now
// lives here instead, one level down, for a reason that matters: this seam is called
// from the worker thread AND from the UI thread AND from a test binary's main, and
// only the first of those is guaranteed to need it. CoInitializeEx is refcounted per
// thread, so entering an apartment that already exists returns S_FALSE and costs
// nothing -- but the matching CoUninitialize must then be skipped, or this call tears
// down an apartment it did not create. RPC_E_CHANGED_MODE is the third case: the
// thread is already in a different apartment model, which is fine to use and must
// also not be torn down.
class ScopedApartment {
public:
    ScopedApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owns_ = (hr == S_OK);
    }
    ~ScopedApartment() {
        if (owns_) CoUninitialize();
    }
    ScopedApartment(const ScopedApartment&) = delete;
    ScopedApartment& operator=(const ScopedApartment&) = delete;

private:
    bool owns_ = false;
};

// The apartment plus the factories that live inside it, created and destroyed
// together, in that order.
//
// THE ORDER IS THE WHOLE POINT, and it cost a segfault to establish. The first
// version of this file cached both factories in process-lifetime statics -- created
// once, never released -- on the argument that a factory outliving one call is what a
// factory wants. That is true of App's factories and false of these, because the
// apartment here is per CALL: the first call created the WIC factory, used it, and
// then CoUninitialize tore down the apartment the factory belonged to, leaving a
// non-null pointer to a dead object. The second call re-entered an apartment, found
// the cached pointer, and crashed. It passed every assertion on the first call and
// died on the second, which is why a test that calls the seam twice found it and a
// test that called it once would not have.
//
// The shape below is not new -- it is what render.cpp's asyncImageWorker already did
// (verified against 95958fa, not recalled): CoCreateInstance for WIC INSIDE the
// apartment, Release before CoUninitialize. Recreating a factory per call is the cost,
// and it is negligible against what these functions then do -- a file read and a full
// image decode, or two SVG parses and a render-target creation.
//
// withD2D is a parameter rather than two classes because only rasterizeSvg needs the
// Direct2D factory, and creating one in decodeImageFile would be a cost with no
// caller. Members are destroyed in reverse declaration order, so declaring the
// apartment FIRST is what guarantees both factories are released before it closes.
class ImagingScope {
public:
    explicit ImagingScope(bool withD2D) {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) {
            wic_ = nullptr;
        }
        if (withD2D &&
            FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_))) {
            d2d_ = nullptr;
        }
    }
    ~ImagingScope() {
        if (d2d_) d2d_->Release();
        if (wic_) wic_->Release();
    }
    ImagingScope(const ImagingScope&) = delete;
    ImagingScope& operator=(const ImagingScope&) = delete;

    IWICImagingFactory* wic() const { return wic_; }
    ID2D1Factory* d2d() const { return d2d_; }

private:
    ScopedApartment apartment_;  // declared first, destroyed last
    IWICImagingFactory* wic_ = nullptr;
    ID2D1Factory* d2d_ = nullptr;
};

// Copy a WIC bitmap's pixels out as premultiplied BGRA.
//
// This copy is new, and it is the one place this move is not free. loadSvgBitmap
// handed its IWICBitmap straight to CreateBitmapFromWicBitmap, so the pixels never
// passed through user memory; returning DecodedImage means one memcpy of the
// rasterized SVG. It buys a uniform return type across all three entry points and a
// result a test can assert on without a render target, and SVGs are small and cached
// after first load. Said plainly rather than left to be discovered: the local-raster
// path is NOT routed through THIS HELPER for exactly this reason.
//
// Corrected in place rather than deleted, because the sentence above used to read "the
// local-raster path in render.cpp is NOT routed here" and half of that is now false: the
// path IS in this file, as decodeImageFileToBitmap at the bottom. What survives is the
// part that mattered -- it does not come through copyOutPixels, and it never will,
// because bypassing this copy is the entire reason it is a separate entry point.
bool copyOutPixels(IWICBitmap* bitmap, UINT w, UINT h, DecodedImage& out) {
    out.pixels.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(bitmap->CopyPixels(nullptr, w * 4,
                                  static_cast<UINT>(out.pixels.size()),
                                  out.pixels.data()))) {
        out.pixels.clear();
        return false;
    }
    out.width = w;
    out.height = h;
    out.ok = true;
    return true;
}

} // namespace

std::wstring fetchToTempFile(const std::string& url) {
    // URLDownloadToCacheFileW is a COM API and the original ran it inside
    // asyncImageWorker's apartment. Splitting the fetch out of the decode dropped
    // that, which nothing would have reported: the failure mode is a fetch that
    // returns an error on a thread with no apartment, indistinguishable from an
    // unreachable host. Restored here so the guarantee travels with the call.
    ScopedApartment apartment;
    wchar_t tempPath[MAX_PATH] = {};
    const std::wstring wideUrl = toWide(url);
    if (SUCCEEDED(URLDownloadToCacheFileW(nullptr, wideUrl.c_str(), tempPath,
                                          MAX_PATH, 0, nullptr))) {
        return tempPath;
    }
    return std::wstring();
}

DecodedImage decodeImageFile(const std::wstring& path) {
    DecodedImage out;
    ImagingScope scope(false);
    IWICImagingFactory* wic = scope.wic();
    if (!wic) return out;

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand,
                                              &decoder)) ||
        !decoder) {
        return out;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
        IWICFormatConverter* converter = nullptr;
        if (SUCCEEDED(wic->CreateFormatConverter(&converter)) && converter) {
            if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                converter->GetSize(&w, &h);
                if (w > 0 && h > 0 && w < kMaxImageDimension &&
                    h < kMaxImageDimension) {
                    out.pixels.resize(static_cast<size_t>(w) * h * 4);
                    if (SUCCEEDED(converter->CopyPixels(
                            nullptr, w * 4, static_cast<UINT>(out.pixels.size()),
                            out.pixels.data()))) {
                        out.width = w;
                        out.height = h;
                        out.ok = true;
                    } else {
                        out.pixels.clear();
                    }
                }
            }
            converter->Release();
        }
        frame->Release();
    }
    decoder->Release();
    return out;
}

DecodedImage rasterizeSvg(const void* data, size_t size, float scale,
                          float& intrinsicW, float& intrinsicH) {
    DecodedImage out;
    intrinsicW = 0.0f;
    intrinsicH = 0.0f;
    if (!data || size == 0 || scale <= 0.0f) return out;

    ImagingScope scope(true);
    IWICImagingFactory* wic = scope.wic();
    ID2D1Factory* d2d = scope.d2d();
    if (!wic || !d2d) return out;

    // A WIC-backed render target queried up to ID2D1DeviceContext5, which is where
    // Direct2D's SVG support lives (Windows 10 1703+). The QueryInterface failing is
    // not an error to report -- it is a pre-1703 machine, and the caller's alt-text
    // path is the intended outcome.
    auto makeDc5 = [&](UINT w, UINT h, IWICBitmap** wicOut,
                       ID2D1DeviceContext5** dcOut) -> bool {
        *wicOut = nullptr;
        *dcOut = nullptr;
        if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnDemand, wicOut))) {
            return false;
        }
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ID2D1RenderTarget* rt = nullptr;
        if (FAILED(d2d->CreateWicBitmapRenderTarget(*wicOut, props, &rt))) {
            (*wicOut)->Release();
            *wicOut = nullptr;
            return false;
        }
        HRESULT hr = rt->QueryInterface(IID_PPV_ARGS(dcOut));
        rt->Release();
        if (FAILED(hr) || !*dcOut) {
            (*wicOut)->Release();
            *wicOut = nullptr;
            return false;
        }
        return true;
    };
    auto makeStream = [&]() -> IStream* {
        return SHCreateMemStream(static_cast<const BYTE*>(data),
                                 static_cast<UINT>(size));
    };

    // Pass 1: parse only, to learn the intrinsic size
    float svgW = 0.0f, svgH = 0.0f;
    {
        IWICBitmap* tmp = nullptr;
        ID2D1DeviceContext5* dc = nullptr;
        if (!makeDc5(4, 4, &tmp, &dc)) return out;
        IStream* stream = makeStream();
        ID2D1SvgDocument* doc = nullptr;
        if (stream &&
            SUCCEEDED(dc->CreateSvgDocument(stream, D2D1::SizeF(512, 512), &doc)) &&
            doc) {
            ID2D1SvgElement* root = nullptr;
            doc->GetRoot(&root);
            if (root) {
                FLOAT w = 0, h = 0;
                if (SUCCEEDED(root->GetAttributeValue(L"width", &w)) &&
                    SUCCEEDED(root->GetAttributeValue(L"height", &h)) && w > 0 &&
                    h > 0) {
                    svgW = w;
                    svgH = h;
                } else {
                    D2D1_SVG_VIEWBOX vb{};
                    if (SUCCEEDED(root->GetAttributeValue(
                            L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &vb,
                            sizeof(vb))) &&
                        vb.width > 0 && vb.height > 0) {
                        svgW = vb.width;
                        svgH = vb.height;
                    }
                }
                root->Release();
            }
            doc->Release();
        }
        if (stream) stream->Release();
        dc->Release();
        tmp->Release();
    }
    if (svgW <= 0.0f || svgH <= 0.0f) {
        svgW = 300.0f;  // the SVG spec's replaced-element default
        svgH = 150.0f;
    }
    svgW = std::min(svgW, 4096.0f);
    svgH = std::min(svgH, 4096.0f);

    // Pass 2: rasterize at `scale`
    const UINT pxW = static_cast<UINT>(std::ceil(svgW * scale));
    const UINT pxH = static_cast<UINT>(std::ceil(svgH * scale));
    IWICBitmap* wicBitmap = nullptr;
    ID2D1DeviceContext5* dc = nullptr;
    if (!makeDc5(pxW, pxH, &wicBitmap, &dc)) return out;

    bool drawn = false;
    {
        IStream* stream = makeStream();
        ID2D1SvgDocument* doc = nullptr;
        if (stream &&
            SUCCEEDED(dc->CreateSvgDocument(stream, D2D1::SizeF(svgW, svgH), &doc)) &&
            doc) {
            dc->BeginDraw();
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
            dc->DrawSvgDocument(doc);
            drawn = SUCCEEDED(dc->EndDraw());
            doc->Release();
        }
        if (stream) stream->Release();
    }
    dc->Release();

    if (drawn) {
        copyOutPixels(wicBitmap, pxW, pxH, out);
        if (out.ok) {
            intrinsicW = svgW;
            intrinsicH = svgH;
        }
    }
    wicBitmap->Release();
    return out;
}

DecodedImage rasterizeSvgFile(const std::wstring& path, float scale,
                              float& intrinsicW, float& intrinsicH) {
    intrinsicW = 0.0f;
    intrinsicH = 0.0f;
    // The wide-path std::ifstream constructor is an MSVC extension with no portable
    // spelling, which is why this read is per-backend rather than shared. Moved from
    // loadSvgBitmap unchanged, including treating an empty file as a failure.
    std::ifstream file(path, std::ios::binary);
    if (!file) return DecodedImage();
    std::string bytes((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    if (bytes.empty()) return DecodedImage();
    return rasterizeSvg(bytes.data(), bytes.size(), scale, intrinsicW, intrinsicH);
}

// The local-raster branch, moved out of render.cpp:2533-2589 as a call sequence rather
// than rewritten. Every HRESULT test, every Release, and the order of the three releases
// relative to CreateBitmapFromWicBitmap are the original's; what changed is where the WIC
// factory and the render target come from, and nothing else.
//
// THE TWO SUBSTITUTIONS, EACH WITH WHAT IT COSTS
//
// 1. app.wicFactory -> ImagingScope. render.cpp used the App's process-lifetime factory,
//    created once in initD2D. This creates one per call, which is the same trade
//    fetchToTempFile and decodeImageFile above already made and for the same reason: a
//    factory reached through App is a factory this file cannot see. The cost is one
//    CoCreateInstance against a full image decode -- and the apartment is not optional
//    padding, because the original ran on the UI thread where one already existed and
//    this function has no way to know who is calling it.
//
// 2. app.nativeRenderTarget -> the seam's own target, downcast. The original named
//    ID2D1HwndRenderTarget, but CreateBitmapFromWicBitmap is declared on
//    ID2D1RenderTarget, so the HWND-shaped type was never needed for this call -- it was
//    just the pointer that happened to be in reach. Taking the base interface is what
//    lets the reach-through disappear from render.cpp instead of moving.
//
// A missing bitmap and a failed decode are both nullptr, deliberately: the caller sets
// entry.failed = true up front and only clears it on success, so a single failure return
// preserves the original's behaviour for all four of its early exits.
std::shared_ptr<Bitmap> decodeImageFileToBitmap(RenderTarget& target,
                                                const std::wstring& path) {
    // Asserted, not tolerated -- the same shape as nativeTextLayout in text_d2d.cpp, and
    // image_api.h says why: nullptr here is indistinguishable from an unreadable file, so
    // a Qt target arriving in the Windows backend would be diagnosed as a broken image.
    assert(dynamic_cast<D2DRenderTarget*>(&target) != nullptr &&
           "a non-Direct2D RenderTarget reached the Direct2D image backend");
    auto* d2dTarget = dynamic_cast<D2DRenderTarget*>(&target);
    if (!d2dTarget || !d2dTarget->native()) return nullptr;

    ImagingScope scope(false);
    IWICImagingFactory* wic = scope.wic();
    if (!wic) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    hr = wic->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        return nullptr;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return nullptr;
    }

    ID2D1Bitmap* bitmap = nullptr;
    hr = d2dTarget->native()->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    converter->Release();
    frame->Release();
    decoder->Release();

    if (FAILED(hr) || !bitmap) return nullptr;
    // CreateBitmapFromWicBitmap hands over the only reference, so the wrapper takes it.
    return std::make_shared<D2DOwnedBitmap>(bitmap);
}

std::shared_ptr<Bitmap> decodeIconBytesToBitmap(RenderTarget& target, const void* data,
                                                size_t size, int pixelSize) {
    assert(dynamic_cast<D2DRenderTarget*>(&target) != nullptr &&
           "a non-Direct2D RenderTarget reached the Direct2D image backend");
    auto* d2dTarget = dynamic_cast<D2DRenderTarget*>(&target);
    if (!d2dTarget || !d2dTarget->native()) return nullptr;
    if (!data || size == 0 || pixelSize <= 0) return nullptr;

    ImagingScope scope(false);
    IWICImagingFactory* wic = scope.wic();
    if (!wic) return nullptr;

    // A WIC stream OVER the caller's bytes, which it does not copy -- so the buffer must
    // outlive the stream. Every caller passes kAppIconIco, a static array, so that holds
    // trivially; a caller passing a temporary would be the way to break this.
    IWICStream* stream = nullptr;
    HRESULT hr = wic->CreateStream(&stream);
    if (FAILED(hr) || !stream) return nullptr;
    hr = stream->InitializeFromMemory(
        static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size));
    if (FAILED(hr)) {
        stream->Release();
        return nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                     &decoder);
    if (FAILED(hr) || !decoder) {
        stream->Release();
        return nullptr;
    }

    // THE FRAME CHOICE IS image_api.h's POLICY, IMPLEMENTED, NOT WIC's DEFAULT.
    // GetFrame(0) is what every other decode path in this file does and it is WRONG here:
    // an .ico is a container of independent sizes, frame 0 is whichever the authoring tool
    // wrote first, and Qt's ICO handler picks by a different rule again. Enumerating and
    // applying the stated rule is the only way the two backends agree at a size that has
    // no exact frame -- which the app icon's hero size, 64, is.
    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0) {
        decoder->Release();
        stream->Release();
        return nullptr;
    }

    UINT bestIndex = 0;
    UINT bestWidth = 0;
    bool haveAtOrAbove = false;
    for (UINT i = 0; i < frameCount; ++i) {
        IWICBitmapFrameDecode* probe = nullptr;
        if (FAILED(decoder->GetFrame(i, &probe)) || !probe) continue;
        UINT fw = 0, fh = 0;
        const bool sized = SUCCEEDED(probe->GetSize(&fw, &fh)) && fw > 0;
        probe->Release();
        if (!sized) continue;

        const bool atOrAbove = fw >= static_cast<UINT>(pixelSize);
        if (haveAtOrAbove) {
            // Already holding a big-enough frame: only a smaller big-enough one wins.
            if (atOrAbove && fw < bestWidth) bestWidth = fw, bestIndex = i;
        } else if (atOrAbove) {
            // First big-enough frame always displaces a merely-largest one.
            haveAtOrAbove = true;
            bestWidth = fw;
            bestIndex = i;
        } else if (fw > bestWidth) {
            // No big-enough frame yet, so track the largest as the documented fallback.
            bestWidth = fw;
            bestIndex = i;
        }
    }
    if (bestWidth == 0) {
        decoder->Release();
        stream->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(bestIndex, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        stream->Release();
        return nullptr;
    }

    // Scale to exactly pixelSize. Unconditional rather than skipped when the frame already
    // matches: WICBitmapInterpolationModeFant on an identity scale is a copy, and branching
    // would give the exact-frame size a different code path from the scaled one -- which is
    // precisely the asymmetry that would let a test at 32 pass while 64 was broken.
    IWICBitmapScaler* scaler = nullptr;
    hr = wic->CreateBitmapScaler(&scaler);
    if (FAILED(hr) || !scaler) {
        frame->Release();
        decoder->Release();
        stream->Release();
        return nullptr;
    }
    hr = scaler->Initialize(frame, static_cast<UINT>(pixelSize),
                            static_cast<UINT>(pixelSize),
                            WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        scaler->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    hr = wic->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        scaler->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        return nullptr;
    }
    hr = converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        scaler->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        return nullptr;
    }

    ID2D1Bitmap* bitmap = nullptr;
    hr = d2dTarget->native()->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    converter->Release();
    scaler->Release();
    frame->Release();
    decoder->Release();
    stream->Release();

    if (FAILED(hr) || !bitmap) return nullptr;
    return std::make_shared<D2DOwnedBitmap>(bitmap);
}

} // namespace tinta

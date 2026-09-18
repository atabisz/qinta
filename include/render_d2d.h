#ifndef TINTA_RENDER_D2D_H
#define TINTA_RENDER_D2D_H

// THIS HEADER IS DECLARATION-ONLY OFF WINDOWS, AND THE GUARD MOVED TO THE .CPP
// TO SAY SO PRECISELY.
//
// It used to `#error` here, on the grounds that off Windows you want
// render_qt.h. That was aimed at the right mistake but caught the wrong event:
// app.h embeds D2DRenderTarget, D2DBrush, D2DStrokeStyle and D2DBitmap BY VALUE,
// so a non-Windows compiler cannot even declare App without this header, and the
// guard was gating a *declaration* rather than a *use*.
//
// Nothing here can be used off Windows, and that is a measured property rather
// than a promise: every wrapper in this file only STORES a native pointer, and
// there are ZERO inline dereferences of one -- every method that touches Direct2D
// is out-of-line in render_d2d.cpp, which is where the #error now lives. So off
// Windows the classes below declare cleanly against forward declarations and the
// first attempt to actually build the backend still fails loudly.
#ifdef _WIN32
// Before render_api.h, not for tidiness: that header hard-errors without the
// DrawText macro in scope, because a TU that declares RenderTarget::DrawText while
// its neighbours declare RenderTarget::DrawTextW has an ODR violation the linker
// need not diagnose. render_api.h explains it at length.
#include <windows.h>
#endif

#include "render_api.h"
#include "text_d2d.h"

#ifdef _WIN32
#include <d2d1.h>
#else
// The SIX Direct2D interfaces this header names, and it names them ONLY as
// pointers -- measured at 16 occurrences, 0 by value. A forward declaration is
// therefore sufficient, and deliberately leaves them incomplete so that any
// inline use added later fails here instead of compiling into a portable build.
//
// It said "four" and "12" until the path seam landed, which is the ordinary way a
// count in a comment goes stale: the number was right when written and nothing can
// re-check it. The two additions are the path pair, and they are the reason
// D2DPathGeometry's destructor is OUT OF LINE -- releasing an incomplete type is
// exactly the inline use this forward declaration exists to reject.
struct ID2D1RenderTarget;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1Bitmap;
struct ID2D1PathGeometry;
struct ID2D1GeometrySink;
#endif

// The Direct2D backend for the drawing seam.
//
// WHY THIS EXISTS WHEN DIRECT2D IS ALREADY WHAT THE APP CALLS
//
// include/render_api.h claims that Direct2D's drawing calls are the portable
// shape rather than a Windows-specific one -- that is why it keeps their names
// and signatures, and why roughly 915 call sites are expected to compile
// unchanged against it. Until this file existed the seam had exactly ONE
// implementation, and an interface with one implementation is not an abstraction,
// it is indirection: nothing had checked that D2D itself fits back through it.
// Every mismatch would have surfaced later, during the rewire, mixed in with a
// 915-site diff.
//
// So this backend is not preparation for the port -- it is the test of the seam's
// central claim, and it is deliberately written second, against an interface
// that was shaped by a non-Windows backend.
//
// OWNERSHIP IS BORROWED, WHICH IS THE OPPOSITE OF THE QT BACKEND
//
// App already owns its ID2D1HwndRenderTarget and its scratch ID2D1SolidColorBrush
// and already Releases them (271 Release() sites in the tree are exactly this
// work). So these wrappers BORROW: they neither AddRef nor Release, and they must
// not outlive what they wrap. QtRenderTarget goes the other way and begins a
// painting session in its constructor, because Qt's QPainter has no separate
// device object to hold.
//
// For the same reason this class does NOT call BeginDraw or EndDraw. Device
// lifetime is deliberately outside the seam (render_api.h says why: EndDraw
// returns an HRESULT that callers inspect), and main_d2d.cpp already owns that
// pair. A wrapper that quietly began its own frame would nest inside the real one.

// Values chosen to match, asserted in render_d2d.cpp where <d2d1.h> is available.
// render_api.h cannot make this assertion itself without including d2d1.h, which
// would defeat the entire point of the header.

class D2DStrokeStyle final : public tinta::StrokeStyle {
public:
    explicit D2DStrokeStyle(ID2D1StrokeStyle* style) : style_(style) {}
    ID2D1StrokeStyle* native() const { return style_; }

private:
    ID2D1StrokeStyle* style_;  // borrowed
};

// THE ONE OWNING WRAPPER IN THIS FILE, and the header comment above says the
// opposite, so this is a stated exception rather than an inconsistency.
//
// Everything else here borrows because App already owns and already Releases the
// thing being wrapped. A path geometry has no such owner: it did not exist before
// BeginPath was called, and the only member that holds one
// (App::LayoutShape::geometry) is now a unique_ptr precisely so that destroying a
// shape destroys its path. Borrowing would leave nobody to Release it -- the two
// manual release loops that used to do that job (App::clearLayoutCache and the
// partial-rollback loop in render.cpp) are deleted in the same commit, because a
// unique_ptr member makes both of them the compiler's work.
//
// The destructor is out of line for the reason the forward declarations above give:
// off Windows ID2D1PathGeometry is incomplete, and calling Release on an incomplete
// type inline here would be the exact mistake those declarations exist to catch.
class D2DPathGeometry final : public tinta::PathGeometry {
public:
    explicit D2DPathGeometry(ID2D1PathGeometry* geometry) : geometry_(geometry) {}
    ~D2DPathGeometry() override;

    D2DPathGeometry(const D2DPathGeometry&) = delete;
    D2DPathGeometry& operator=(const D2DPathGeometry&) = delete;

    ID2D1PathGeometry* native() const { return geometry_; }

private:
    ID2D1PathGeometry* geometry_;  // OWNED -- see above
};

// Owns the sink for as long as the path is open, and the geometry until Close hands
// it over. Both are out-of-line for the incomplete-type reason above.
//
// A sink that is never Closed still has to Release both, which is why the
// destructor exists rather than trusting Close to run: three of the six builders
// this replaces have an early-return path between Open and Close.
class D2DPathSink final : public tinta::PathSink {
public:
    D2DPathSink(ID2D1PathGeometry* geometry, ID2D1GeometrySink* sink)
        : geometry_(geometry), sink_(sink) {}
    ~D2DPathSink() override;

    D2DPathSink(const D2DPathSink&) = delete;
    D2DPathSink& operator=(const D2DPathSink&) = delete;

    void BeginFigure(tinta::PointF start) override;
    void AddLine(tinta::PointF point) override;
    void AddLines(const tinta::PointF* points, uint32_t count) override;
    void AddArc(const tinta::ArcSegment& arc) override;
    void EndFigure() override;
    std::unique_ptr<tinta::PathGeometry> Close() override;

private:
    ID2D1PathGeometry* geometry_;  // OWNED until Close
    ID2D1GeometrySink* sink_;      // OWNED until Close
};

// Asserts the handle really came from this backend, the same guarantee and for the
// same reason as text_d2d.h's nativeTextLayout: a QtPathGeometry arriving at
// D2DRenderTarget::FillGeometry must trip rather than draw nothing.
ID2D1PathGeometry* nativePathGeometry(tinta::PathGeometry* geometry);

// Same guarantee for bitmaps, and exported for the same reason nativePathGeometry
// is: src/print.cpp draws app.layoutBitmaps through its OWN ID2D1DeviceContext,
// which this seam does not wrap, so it needs the native pointer back out. That
// became necessary when LayoutBitmap::bitmap stopped being an ID2D1Bitmap* --
// tests/bitmap_seam_tests.cpp used to static_assert that it never would, giving
// print.cpp's device context as the reason, and this function is that reason
// answered rather than overruled.
//
// Accepts an owned bitmap as well as a borrowed one, because D2DOwnedBitmap derives
// from D2DBitmap precisely so that one unwrapper serves both.
ID2D1Bitmap* nativeBitmap(tinta::Bitmap* bitmap);

// BORROWS. The 2 remaining native holders of a bitmap (nativeLinkPeekBitmap and the
// print-preview local) AddRef and Release their own, and d2d_init.cpp tears the member
// down. A wrapper that took a reference would make the ownership of those depend on
// whether a drawing call had happened to be made through the seam.
//
// It said 4, and the two app icons left when they became shared_ptr<tinta::Bitmap>
// members -- so the borrowing rule now covers fewer holders than it did, and the count
// is stated rather than left as "the remaining ones" precisely because it keeps falling.
class D2DBitmap : public tinta::Bitmap {
public:
    explicit D2DBitmap(ID2D1Bitmap* bitmap) : bitmap_(bitmap) {}

    tinta::SizeF GetSize() const override;

    ID2D1Bitmap* native() const { return bitmap_; }

    // Not final any more, so the rule of five is stated rather than inherited by
    // luck: copying a borrowed wrapper is harmless, copying an OWNING one would
    // double-release, and the owning subclass deletes them for itself.
    D2DBitmap(const D2DBitmap&) = default;
    D2DBitmap& operator=(const D2DBitmap&) = default;

protected:
    ID2D1Bitmap* bitmap_;  // borrowed
};

// OWNS its bitmap: it came from D2DRenderTarget::CreateBitmap, which hands the
// caller the only reference. Derives from D2DBitmap rather than reimplementing it so
// that nativeBitmap's single dynamic_cast covers both, which is what keeps
// src/print.cpp's unwrap from having to know which kind it was given.
//
// GetSize is inherited deliberately -- the size question does not depend on who owns
// the pixels, and an override would be the same body twice.
class D2DOwnedBitmap final : public D2DBitmap {
public:
    explicit D2DOwnedBitmap(ID2D1Bitmap* bitmap) : D2DBitmap(bitmap) {}
    ~D2DOwnedBitmap() override;

    D2DOwnedBitmap(const D2DOwnedBitmap&) = delete;
    D2DOwnedBitmap& operator=(const D2DOwnedBitmap&) = delete;
};

class D2DBrush final : public tinta::Brush {
public:
    explicit D2DBrush(ID2D1SolidColorBrush* brush) : brush_(brush) {}

    void SetColor(const tinta::Color& color) override;
    tinta::Color GetColor() const override;

    ID2D1SolidColorBrush* native() const { return brush_; }

private:
    ID2D1SolidColorBrush* brush_;  // borrowed
};

class D2DRenderTarget final : public tinta::RenderTarget {
public:
    explicit D2DRenderTarget(ID2D1RenderTarget* target) : target_(target) {}

    void Clear(const tinta::Color& color) override;

    void FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) override;
    void DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                       float strokeWidth) override;

    void FillRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush) override;
    void DrawRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush,
                              float strokeWidth) override;

    void FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) override;
    void DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                     float strokeWidth) override;

    void DrawLine(tinta::PointF start, tinta::PointF end, tinta::Brush* brush,
                  float strokeWidth, tinta::StrokeStyle* strokeStyle) override;

    std::unique_ptr<tinta::PathSink> BeginPath() override;
    void FillGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush) override;
    void DrawGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush,
                      float strokeWidth) override;

    // Reads as DrawTextW here, because <windows.h> above has already rewritten the
    // token -- in this header, in render_api.h, and at all 107 call sites alike.
    void DrawText(const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
                  const tinta::RectF& layoutRect, tinta::Brush* brush,
                  tinta::DrawTextOptions options) override;
    void DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                        tinta::Brush* brush, tinta::DrawTextOptions options) override;

    void DrawBitmap(tinta::Bitmap* bitmap, const tinta::RectF& destRect, float opacity,
                    tinta::BitmapInterpolationMode interpolationMode,
                    const tinta::RectF* srcRect) override;

    std::unique_ptr<tinta::Bitmap> CreateBitmap(uint32_t pixelWidth, uint32_t pixelHeight,
                                               const uint8_t* premultipliedBgra,
                                               uint32_t stride, float dpi) override;

    void PushAxisAlignedClip(const tinta::RectF& rect, tinta::AntialiasMode mode) override;
    void PopAxisAlignedClip() override;

    void SetTransform(const tinta::Matrix3x2F& transform) override;
    void GetTransform(tinta::Matrix3x2F* transform) override;

    ID2D1RenderTarget* native() const { return target_; }

private:
    ID2D1RenderTarget* target_;  // borrowed
};

#endif  // TINTA_RENDER_D2D_H

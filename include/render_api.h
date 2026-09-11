#ifndef TINTA_RENDER_API_H
#define TINTA_RENDER_API_H

#include "geometry.h"
#include "text_api.h"

// THE DrawText MACRO, WHICH IS NOT A NUISANCE BUT AN ODR HAZARD
//
// <winuser.h>:8268 does `#define DrawText DrawTextW` under UNICODE, and every
// tinta target compiles with UNICODE defined. That macro rewrites the token
// everywhere it appears afterwards -- the DECLARATION below as well as all 107
// call sites -- so a translation unit that has seen <windows.h> declares
// `RenderTarget::DrawTextW` and calls `DrawTextW`, which is self-consistent and
// works. Direct2D relies on exactly this: ID2D1RenderTarget::DrawText is spelled
// plainly in d2d1.h, and src/editor.cpp:2767 already calls it as `DrawTextW`
// having reached it through the macro.
//
// The hazard is a MIXTURE. A Windows TU that includes this header WITHOUT
// <windows.h> declares `DrawText` instead, so the two TUs disagree about the name
// and the vtable slot of the same virtual function on the same class. That is an
// ODR violation the linker is not obliged to diagnose, and the failure it would
// produce -- a call landing in the wrong virtual slot -- would look like a
// rendering bug rather than a build error.
//
// So this is a hard requirement rather than a convention, and it fails at the
// point that would cause the mismatch rather than somewhere downstream. Off
// Windows there is no macro and nothing to reconcile.
#if defined(_WIN32) && !defined(DrawText)
#error "On Windows, include <windows.h> before render_api.h: the DrawText macro must be visible here and at every call site, or RenderTarget::DrawText and ::DrawTextW become different virtual functions in different translation units."
#endif

// The drawing seam: what a rendering backend has to provide, with no backend in it.
//
// WHY THIS SHAPE, AND WHY THESE METHOD NAMES
//
// Measured on 2026-09-11, over src+include: there are 116 distinct COM methods
// called through 1,836 sites (`rg '->[A-Z][A-Za-z0-9_]*\('`, whose PascalCase
// pattern is a reliable discriminator here because tinta's own member functions
// are camelCase -- the whole tree has 48 camelCase `->` calls, several of them
// std::). That surface is extremely top-heavy:
//
//   SetColor 474 · Release 271 · FillRoundedRectangle 131 · DrawLine 111
//   DrawText 106 · DrawRoundedRectangle 70 · FillRectangle 60 · GetMetrics 40
//   CreateTextFormat 37 · CreateTextLayout 36        -- 73% of all sites
//
// So the number that matters for the port is 116 methods, not 1,836 sites, and
// 271 of those sites are Release(): COM lifetime management that a portable
// backend DELETES rather than reimplements.
//
// The method names here are deliberately the Direct2D ones, PascalCase and all.
// That is not deference to Windows, it is what makes the port mechanical: the
// 0e54418 geometry sweep already replaced every argument type with a portable
// one, so a call site today reads
//
//     app.renderTarget->FillRoundedRectangle(tinta::roundedRect(rect, 2, 2), app.brush);
//
// where the ONLY non-portable things left are the receiver and the brush. Give
// those two portable types with matching signatures and roughly 915 sites compile
// unchanged -- half the total surface -- with no diff to review and no chance of
// a transcription error in a hand-edited paint path. Renaming to a Qt-flavoured
// or house-flavoured API is a later, separable, purely cosmetic pass; doing it in
// the same step as introducing a second backend would mean a 915-site diff in
// which a real behavioural change could hide.
//
// WHAT IS DELIBERATELY NOT HERE
//
//   * The text TYPES. DrawText and DrawTextLayout are here, but TextFormat,
//     TextLayout, TextMetrics and the font enums live in text_api.h, which this
//     header includes. That split is not tidiness: text_api.h is usable on its
//     own by the 73 sites that create and measure a layout without drawing it,
//     and the reasoning behind its cut -- which turned out to contradict the plan
//     of record -- is long enough to deserve its own file to be argued in.
//   * Text ANTIALIASING. SetTextAntialiasMode and SetTextRenderingParams are one
//     site each, both in src/d2d_init.cpp, and both are ClearType gamma tuning on
//     a device this seam does not own. d2d_init.cpp is the Direct2D backend's own
//     construction and will never be portable, so those two sites keep talking to
//     the Direct2D object directly rather than being smuggled through a portable
//     interface that only one backend could mean anything by.
//   * Object creation and device lifetime. BeginDraw/EndDraw, CreateSolidColorBrush,
//     resize and device-loss handling are backend-specific in kind, not just in
//     implementation: D2D's EndDraw returns an HRESULT that callers inspect, and
//     a portable spelling of that is a design question rather than a rename. A
//     concrete backend owns its own construction; this interface is about the
//     drawing calls only.
//   * Bitmap CREATION. CreateBitmapFromWicBitmap 4 and CreateBitmap 2, all six on
//     the same ground as the brush above: they hand back an OWNED object, and six
//     App members hold the result as an AddRef'd ID2D1Bitmap*, so a portable
//     spelling is an ownership redesign rather than a rename. The four WIC calls
//     carry a second, independent reason -- and it is smaller than it was long
//     described as being. Two of them (src/startpage.cpp, src/tabs.cpp) convert a
//     Win32 HICON, so they are Windows-terminal no matter which backend draws;
//     the other two decode an image file, which is a dependency decision. The two
//     CreateBitmap calls, by contrast, take a plain CPU BGRA buffer with a stride
//     and touch no WIC type at all: "bitmaps are WIC-coupled" was true of 4 sites
//     and false of the other 9, which is why DrawBitmap is now here and its
//     creation is not.
//   * Geometry paths (FillGeometry 2, DrawGeometry 2). They need a path object,
//     which is a handle type like StrokeStyle rather than a value type.
//
// SetTransform/GetTransform WERE on this list. They are now in the interface, and
// tinta::Matrix3x2F in geometry.h is the 3x2 matrix type they needed. DrawBitmap
// was on it too, and tinta::Bitmap below is the handle type it needed.
//
// Naming stays qualified as tinta::, for the reason geometry.h gives: bare Color
// is already ambiguous across most of src/.

namespace tinta {

// Values match D2D1_ANTIALIAS_MODE so the Windows backend can cast rather than
// switch, and so the call sites that name a D2D1_ANTIALIAS_MODE_* constant today
// keep meaning exactly what they meant. render_d2d will static_assert the pairing
// the way geometry.h asserts its layouts -- an assertion cannot live here,
// because this header must not include <d2d1.h>.
//
// BOTH VALUES ARE LIVE, and an earlier version of this comment said otherwise. It
// claimed all 9 PushAxisAlignedClip sites pass D2D1_ANTIALIAS_MODE_ALIASED. Five
// do (src/editor.cpp x2, src/editrail.cpp, src/main_d2d.cpp, src/overlays.cpp);
// the other four pass D2D1_ANTIALIAS_MODE_PER_PRIMITIVE (src/overlays.cpp x3,
// src/tabs.cpp). The method count was right and the uniformity was invented. It
// matters because render_qt.cpp branches on this value, and a reader who believed
// the old comment would have been entitled to delete the branch -- silently
// hardening four antialiased clips.
enum class AntialiasMode {
    PerPrimitive = 0,
    Aliased = 1,
};

// Values match D2D1_BITMAP_INTERPOLATION_MODE, asserted in render_d2d.cpp for the
// same reason and in the same place as AntialiasMode's pairing.
//
// The default below is Linear because that is Direct2D's own default for the
// 2- and 3-argument DrawBitmap overloads, and because the three sites that name a
// mode all name Linear -- so every one of the 7 sites means Linear today. Nearest
// is here anyway rather than being dropped as unused: it is the only value that
// makes the interpolation argument OBSERVABLE, so the shared assertions need it to
// prove a backend reads the argument at all instead of hardcoding smooth scaling.
enum class BitmapInterpolationMode {
    NearestNeighbor = 0,
    Linear = 1,
};

// An opaque handle. Dash patterns are built once at startup and only ever passed
// back to DrawLine (2 sites, both `dashed ? style : nullptr`), so nothing in the
// tree needs to READ one -- which is why this has no methods rather than a
// guessed-at accessor.
class StrokeStyle {
public:
    virtual ~StrokeStyle() = default;
};

// A drawable image. Opaque like StrokeStyle, but with ONE accessor, and the
// method list is a measurement rather than a guess: over every bitmap-typed
// receiver in src+include the tree calls Release 15 times, GetSize 3, AddRef once
// and CopyPixels once. Release and AddRef are COM reference counting, which a
// portable backend DELETES rather than reimplements; CopyPixels is the DOCX export
// path, which reads pixels out rather than drawing them. That leaves GetSize.
//
// Creating one is NOT here, deliberately -- see the exclusion list above. A
// backend creates its own bitmaps the way it creates its own brushes, and the
// tests get theirs from RenderHarness::makeBitmap for exactly that reason.
class Bitmap {
public:
    virtual ~Bitmap() = default;

    // In DIPs, matching ID2D1Bitmap::GetSize rather than GetPixelSize: the two
    // differ on a bitmap whose dpi is not 96, and src/render.cpp sets 192 on the
    // rasterized-SVG path. Returning pixels there would halve every SVG.
    virtual SizeF GetSize() const = 0;
};

// The 474 SetColor sites are the single largest item in the whole COM surface,
// and they are all this: recolour the one scratch brush, then draw. GetColor
// exists because a backend that is handed a Brush* has to be able to ask what
// colour it is; Direct2D reads it back the same way.
class Brush {
public:
    virtual ~Brush() = default;
    virtual void SetColor(const Color& color) = 0;
    virtual Color GetColor() const = 0;
};

// Every drawing call tinta makes.
//
// The defaulted strokeWidth and strokeStyle mirror Direct2D's own defaults, so
// the 3-argument DrawLine calls that make up the bulk of the 111 sites and the
// 5-argument dashed-connector calls in main_d2d.cpp and print.cpp both bind
// without edits.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    virtual void Clear(const Color& color) = 0;

    virtual void FillRectangle(const RectF& rect, Brush* brush) = 0;
    virtual void DrawRectangle(const RectF& rect, Brush* brush,
                               float strokeWidth = 1.0f) = 0;

    virtual void FillRoundedRectangle(const RoundedRect& rect, Brush* brush) = 0;
    virtual void DrawRoundedRectangle(const RoundedRect& rect, Brush* brush,
                                      float strokeWidth = 1.0f) = 0;

    virtual void FillEllipse(const Ellipse& ellipse, Brush* brush) = 0;
    virtual void DrawEllipse(const Ellipse& ellipse, Brush* brush,
                             float strokeWidth = 1.0f) = 0;

    virtual void DrawLine(PointF start, PointF end, Brush* brush,
                          float strokeWidth = 1.0f,
                          StrokeStyle* strokeStyle = nullptr) = 0;

    // The two largest text items in the tree: 106 DrawText sites and 26
    // DrawTextLayout, every one of them on app.renderTarget. They are the reason
    // this seam could not reach App at all before now -- a C++ member has one
    // static type, so App::renderTarget cannot become a tinta::RenderTarget*
    // until the calls made THROUGH it are all expressible here.
    //
    // The defaulted options argument matters: 104 of the 106 DrawText sites pass
    // nothing, and the two that do (main_d2d.cpp, math_render.cpp) pass
    // EnableColorFont. Both forms bind without an edit.
    virtual void DrawText(const wchar_t* string, uint32_t stringLength,
                          TextFormat* format, const RectF& layoutRect, Brush* brush,
                          DrawTextOptions options = DrawTextOptions::None) = 0;

    virtual void DrawTextLayout(PointF origin, TextLayout* layout, Brush* brush,
                                DrawTextOptions options = DrawTextOptions::None) = 0;

    // 7 sites, called THREE ways, and the defaults are what make all three bind
    // without an edit: 2 arguments (src/editrail.cpp, src/startpage.cpp,
    // src/tabs.cpp -- an icon at its natural size), 4 (src/overlays.cpp:2525, the
    // print preview), and 5 with a trailing null srcRect (src/overlays.cpp:1366 and
    // :1425, the link peek and the lightbox). The defaults are Direct2D's own, so a
    // 2-argument call means opacity 1 and linear interpolation on both backends.
    //
    // srcRect is a POINTER, not a defaulted value, because null means "all of it"
    // and that is a distinct state from any rectangle -- there is no rectangle that
    // means "whatever size this bitmap turns out to be". Both sites that pass the
    // argument today pass null, so the sub-rect path is exercised only by the shared
    // assertions; it is implemented rather than stubbed because a backend that
    // ignored srcRect would draw a plausible picture of the wrong region.
    virtual void DrawBitmap(Bitmap* bitmap, const RectF& destRect, float opacity = 1.0f,
                            BitmapInterpolationMode interpolationMode =
                                BitmapInterpolationMode::Linear,
                            const RectF* srcRect = nullptr) = 0;

    // Clips nest, and every push in the tree is matched by a pop in the same
    // function, so a backend may implement these with a save/restore stack.
    virtual void PushAxisAlignedClip(const RectF& rect, AntialiasMode mode) = 0;
    virtual void PopAxisAlignedClip() = 0;

    // The transform is REPLACED, not composed. Every call site in the tree already
    // does its own composing -- the pattern is GetTransform into a local, then
    // SetTransform(something * previous), then SetTransform(previous) to restore --
    // so a seam that composed instead would double-apply at all 10 renderTarget
    // sites. Qt's setWorldTransform has a combine flag defaulting to replace, and
    // that default is the one this seam means.
    //
    // GetTransform takes an out-pointer rather than returning a value because that
    // is Direct2D's signature and the six sites read
    // `GetTransform(&previous)`. Non-const because ID2D1RenderTarget::GetTransform
    // is non-const on the wrapper this seam delegates to.
    virtual void SetTransform(const Matrix3x2F& transform) = 0;
    virtual void GetTransform(Matrix3x2F* transform) = 0;
};

}  // namespace tinta

#endif  // TINTA_RENDER_API_H

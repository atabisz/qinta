#ifndef TINTA_RENDER_API_SHARED_H
#define TINTA_RENDER_API_SHARED_H

// WINDOWS.H FIRST, AND NOT FOR TIDINESS.
//
// render_api.h hard-errors on Windows unless the DrawText macro is already in
// scope, because <windows.h> defines DrawText as DrawTextW: a translation unit that
// has seen windows.h declares RenderTarget::DrawTextW, one that has not declares
// RenderTarget::DrawText, and those are different virtual functions in different
// vtable slots for the same class. The linker is not obliged to notice.
//
// Every one of the 106 app.renderTarget->DrawText call sites is in a file that
// already pulls in windows.h through app.h, so the macro is in scope at all of them
// and they expand the same way the declaration does. THIS header is the exception
// that has to say so out loud, because tests/render_api_shared.cpp is compiled into
// both a Windows and a non-Windows target from the same source -- on Windows the
// call and the declaration are both DrawTextW, off Windows both are DrawText, and
// the one thing that must never happen is a mixture.
#ifdef _WIN32
#include <windows.h>
#endif

#include "render_api.h"

#include <memory>
#include <vector>

// ONE ASSERTION SET, EVERY BACKEND.
//
// render_api.h claims Direct2D's drawing calls are a portable shape rather than a
// Windows-specific one. Two backends each with their own tests would only prove
// that each one is self-consistent; the interesting failure is the two DISAGREEING
// -- a rounded corner that is round under Qt and square under Direct2D, a clip that
// leaks on one side only, a colour that rounds differently. That is invisible
// unless the same assertions, in the same order, with the same messages, run
// against both. So the assertions live here and the backends supply a harness.
//
// A backend joins by implementing RenderHarness. Nothing in the assertion body
// names Qt, Direct2D, QImage or IWICBitmap.

// Straight (NOT premultiplied) 8-bit RGBA. Straight alpha is the interchange
// format on purpose: Qt's Format_ARGB32 is already straight, Direct2D's WIC target
// is premultiplied, and a premultiplied comparison would let a wrong colour and a
// wrong alpha cancel each other out. The D2D harness un-premultiplies on readback
// so both sides mean the same thing by "half-transparent red".
struct Rgba {
    int r = 0, g = 0, b = 0, a = 0;

    bool operator==(const Rgba& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Rgba& other) const { return !(*this == other); }
};

constexpr int kWidth = 200;
constexpr int kHeight = 120;

const Rgba kWhite = Rgba{255, 255, 255, 255};
const Rgba kRed = Rgba{255, 0, 0, 255};
const Rgba kBlue = Rgba{0, 0, 255, 255};

class RenderHarness {
public:
    virtual ~RenderHarness() = default;

    // A kWidth x kHeight surface, fully transparent, discarding any previous one.
    virtual void newSurface() = 0;

    // Open a drawing session on the CURRENT contents and return the target. The
    // split matters: several assertions draw, read the pixels back, then draw
    // again over the result, and a begin() that implicitly cleared would make the
    // second half of those tests meaningless.
    virtual tinta::RenderTarget& begin() = 0;

    // Close the session. Pixels are only guaranteed readable after this -- Qt has
    // to destroy its QPainter and Direct2D has to EndDraw.
    virtual void end() = 0;

    virtual std::unique_ptr<tinta::Brush> makeBrush(const tinta::Color& color) = 0;

    // A drawable bitmap, row-major, `width * height` entries, in the same STRAIGHT
    // (not premultiplied) Rgba the pixel reader returns. Supplied by the harness for
    // the same reason makeBrush is: Direct2D needs a device-owned ID2D1Bitmap and a
    // premultiply, Qt needs only a QImage, and no assertion should know which.
    //
    // THIS STAYS HAND-ROLLED NOW THAT THE SEAM CAN CREATE ONE. The reason used to be
    // that "render_api.h keeps creation out of the seam"; RenderTarget::CreateBitmap
    // is the exception, so that reason has expired and a better one replaced it --
    // the bitmap-creation assertions compare the seam's output against THIS, and two
    // independent construction paths that must produce the same pixels is a stronger
    // instrument than one path checked against a copy of itself. Reimplementing this
    // as a call to CreateBitmap would silently turn that comparison into a tautology.
    //
    // Returning null is a legitimate answer for a harness that cannot allocate one;
    // the assertions check for it and say so rather than dereferencing, because "the
    // instrument could not be built" and "the seam is broken" must not report alike.
    virtual std::unique_ptr<tinta::Bitmap> makeBitmap(int width, int height,
                                                      const std::vector<Rgba>& pixels) = 0;

    virtual Rgba pixel(int x, int y) = 0;

    // The text seam's factory. Supplied by the harness because creating one is a
    // per-backend act -- Direct2D needs a DWriteCreateFactory call and a COM
    // apartment, Qt needs a QGuiApplication -- and neither belongs in an assertion.
    virtual tinta::TextFactory& textFactory() = 0;

    // A font family the harness knows resolves on ITS platform. Segoe UI exists on
    // every Windows install and on no Linux one; DejaVu Sans is the reverse. Hard-
    // coding either in the shared assertions would make one backend measure an empty
    // family, and qt_text_tests.cpp already recorded what that looks like: metrics
    // still come back, well-formed and fabricated, with exit code 0.
    virtual const wchar_t* fontFamily() const = 0;

    // Backend name for the summary line, so a green run says WHICH backend was green.
    virtual const char* name() const = 0;
};

// Runs every assertion against the harness. Returns the number that FAILED, and
// prints a line per failure plus a summary. Also reports the assertion count, so a
// backend silently running fewer of them cannot read as a pass.
int runRenderApiAssertions(RenderHarness& harness);

#endif  // TINTA_RENDER_API_SHARED_H

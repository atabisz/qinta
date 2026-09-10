#ifndef TINTA_RENDER_API_SHARED_H
#define TINTA_RENDER_API_SHARED_H

#include "render_api.h"

#include <memory>

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

    virtual Rgba pixel(int x, int y) = 0;

    // Backend name for the summary line, so a green run says WHICH backend was green.
    virtual const char* name() const = 0;
};

// Runs every assertion against the harness. Returns the number that FAILED, and
// prints a line per failure plus a summary. Also reports the assertion count, so a
// backend silently running fewer of them cannot read as a pass.
int runRenderApiAssertions(RenderHarness& harness);

#endif  // TINTA_RENDER_API_SHARED_H

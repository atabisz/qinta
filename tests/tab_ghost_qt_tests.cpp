// THE QT GHOST, READ PIXEL BY PIXEL.
//
// The Qt arm's whole risk is that it LOOKS finished and draws something subtly different from the
// Windows arm -- a soft corner instead of a hard one, the border colour where the fill should be, a
// card that is transparent everywhere because the paint never ran. None of that shows up in a build,
// and all of it shows up here.
//
// THE SENTINEL IS THE INSTRUMENT. Every image below is pre-filled with opaque magenta before
// tabGhostPaint touches it. An assertion that the corners are transparent is worthless over an
// already-transparent buffer -- it passes when the function does nothing at all, which is the
// commonest way for a paint to be wrong. Against magenta, "corner alpha 0" means the function
// reached that pixel and cleared it, and "centre is the fill colour" means it reached that one and
// painted it.
//
// No window is shown and no event loop runs: QGuiApplication exists only because QFont and QImage
// need one for font metrics. Nothing here creates a TabGhostWidget, so nothing reaches Alex's
// screen -- which is also why this can run under the tree's per-platform QPA pin rather than needing
// its own.
#include "tab_ghost_qt.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

const QRgb kSentinel = qRgba(255, 0, 255, 255);  // opaque magenta: nothing here draws it

QImage sentinelImage(const tinta::TabGhostGeometry& g) {
    QImage image(g.width, g.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(kSentinel);
    return image;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // THE PLATFORM PLUGIN IS NOT FORCED HERE, and the first draft of this file did force
    // `offscreen` -- which would have been wrong on this host for a reason CMakeLists.txt already
    // records around its TINTA_QPA_PLATFORM pin: the offscreen plugin's font database is EMPTY on
    // Windows. This test counts the pixels a title draws, so under offscreen the card would come
    // back textless and the count would read zero -- a green build, a red assertion, and a day
    // spent looking at the seam instead of at the plugin. So the platform comes from the ctest
    // property like every other Qt target here (`windows` on Windows, `offscreen` elsewhere), and
    // the guard below is what makes the difference say so instead of being inferred.
    if (QGuiApplication::platformName().isEmpty()) {
        std::printf("FAIL: no QPA platform plugin -- refusing to report assertions\n");
        return 2;
    }
    // REFUSE RATHER THAN REPORT A BLANK CARD. Without a font database the title draws nothing, the
    // difference count comes back zero, and the failure names the seam instead of the environment --
    // which is the exact substitution qt_text and render_api are already guarded against in this
    // tree. Exit 2, not 1, so a missing font database is distinguishable from a real regression.
    if (QFontDatabase::families().isEmpty()) {
        std::printf("FAIL: QPA platform '%s' has an EMPTY font database, so the title cannot draw. "
                    "This is the environment, not the seam -- refusing to report assertions.\n",
                    QGuiApplication::platformName().toUtf8().constData());
        return 2;
    }
    std::printf("qpa    : %s (%lld font families)\n",
                QGuiApplication::platformName().toUtf8().constData(),
                (long long)QFontDatabase::families().size());

    const tinta::Color fill{0.15f, 0.17f, 0.20f, 1.0f};
    const tinta::Color border{0.80f, 0.82f, 0.85f, 1.0f};
    const tinta::Color text{0.90f, 0.91f, 0.92f, 1.0f};
    const tinta::TabGhostCard card = tinta::tabGhostCard(1.0f, fill, border, text);
    const tinta::TabGhostGeometry& g = card.geometry;

    // --- The sentinel is really there before we start ------------------------
    {
        QImage blank = sentinelImage(g);
        check(blank.pixel(0, 0) == kSentinel && blank.pixel(g.width / 2, g.height / 2) == kSentinel,
              "SENTINEL: the image starts opaque magenta at both a corner and the centre, so every "
              "assertion below is about something tabGhostPaint DID rather than about a buffer that "
              "was already the answer");
    }

    // --- ISC-7: the card the Qt arm actually paints --------------------------
    QImage image = sentinelImage(g);
    tinta::tabGhostPaint(image, card, QStringLiteral("probe-document.md"));

    // SAMPLED BELOW THE TITLE, not at the geometric centre, and that is not fussiness: the title is
    // vertically centred, so the middle of the card is exactly where a glyph is most likely to be.
    // A fill-colour assertion taken there passes or fails on which letters "probe-document.md"
    // happens to contain and on which Segoe UI version is installed. Four rows up from the bottom is
    // below the 13-pixel glyph band and above the one-pixel border.
    const int cx = g.width / 2;
    const int cy = g.height - 4;
    const QRgb centre = image.pixel(cx, cy);
    check(qAlpha(centre) == 255, "QT: the body of the card is fully opaque");
    check(qRed(centre) == 38 && qGreen(centre) == 43 && qBlue(centre) == 51,
          "QT: and is exactly the FILL colour handed in -- 0.15/0.17/0.20 rounded the same way the "
          "Win32 arm rounds it, so the two arms agree on the byte and not merely on the intent");
    check(centre != kSentinel,
          "QT: which is not the sentinel, so the paint reached the middle of the card");

    // The four corners, each one named. A combined count would report "some corner is wrong"
    // without saying which, and a single wrong corner is exactly the defect ISC-4 exists for.
    check(qAlpha(image.pixel(0, 0)) == 0, "QT: the top-left corner pixel is fully transparent");
    check(qAlpha(image.pixel(g.width - 1, 0)) == 0, "QT: so is the top-right");
    check(qAlpha(image.pixel(0, g.height - 1)) == 0, "QT: so is the bottom-left");
    check(qAlpha(image.pixel(g.width - 1, g.height - 1)) == 0, "QT: and so is the bottom-right");
    check(image.pixel(0, 0) == 0u,
          "QT: and the transparent corner is ZERO throughout, not merely alpha 0 over leftover "
          "colour -- this is a premultiplied surface, where colour behind alpha 0 shows as a dark "
          "halo wherever a compositor reads it");

    // A HARD cut, not an antialiased one. This is the assertion that fails if the arm is rewritten
    // the obvious Qt way -- clip to a QPainterPath and paint inside it -- because QPainter
    // antialiases the clip and the pixel just inside the corner comes back part-transparent.
    {
        int fringe = 0;
        for (int py = 0; py < g.height; py++) {
            for (int px = 0; px < g.width; px++) {
                const int a = qAlpha(image.pixel(px, py));
                if (a != 0 && a != 255) fringe++;
            }
        }
        check(fringe == 0,
              "QT: every pixel is either fully opaque or fully clear -- the corner cut is hard, the "
              "way the Win32 per-pixel rebuild makes it, and not an antialiased clip");
    }

    // The card is the same SHAPE as the Windows arm's, measured against the shared predicate rather
    // than against a second opinion about rounded rectangles.
    {
        int wrong = 0;
        for (int py = 0; py < g.height; py++) {
            for (int px = 0; px < g.width; px++) {
                const bool opaque = qAlpha(image.pixel(px, py)) == 255;
                if (opaque != tinta::tabGhostCovers(g, px, py)) wrong++;
            }
        }
        check(wrong == 0,
              "QT: and the opaque region is EXACTLY tabGhostCovers -- the same predicate the Win32 "
              "alpha rebuild calls, so the two arms have one silhouette rather than two that agree");
    }

    // The border must be drawn, and in the border colour. Without this the card could be a plain
    // filled rectangle and every assertion above would still hold.
    {
        const QRgb edge = image.pixel(cx, 0);
        check(qAlpha(edge) == 255 && edge != centre,
              "QT: the top edge at mid-width differs from the fill, so a border was drawn");
        check(qRed(edge) > qRed(centre) && qBlue(edge) > qBlue(centre),
              "QT: and it is lighter than the fill, which is the direction the border colour "
              "handed in actually goes");
    }

    // The title must be drawn -- MEASURED AGAINST THE SAME CARD WITH AN EMPTY TITLE, not against a
    // brightness threshold. A threshold is the obvious way to write this and it does not work: the
    // border colour handed in is 204/209/217, which clears any threshold low enough to catch
    // antialiased glyph edges, and the border crosses the text inset on two rows. The first version
    // of this assertion counted 336 border pixels and would have passed with no text at all.
    //
    // A difference count needs no threshold and no glyph positions, so it is also immune to which
    // Segoe UI is installed.
    auto countDiffering = [&](const QImage& a, const QImage& b) {
        int differing = 0;
        for (int py = 0; py < g.height; py++) {
            for (int px = 0; px < g.width; px++) {
                if (a.pixel(px, py) != b.pixel(px, py)) differing++;
            }
        }
        return differing;
    };
    {
        QImage untitled = sentinelImage(g);
        tinta::tabGhostPaint(untitled, card, QString());
        const int drawn = countDiffering(image, untitled);
        check(drawn > 50,
              "QT: the title was drawn -- the same card with an empty title differs in a glyph's "
              "worth of pixels, which needs no brightness threshold and so cannot be satisfied by "
              "the border");
        // And the empty-title card is still a card: this is what separates "the title drew nothing"
        // from "the paint drew nothing".
        check(qAlpha(untitled.pixel(cx, cy)) == 255 && untitled.pixel(cx, cy) == centre,
              "QT CONTROL: and the untitled card still has an opaque body in the fill colour, so "
              "the count above is the text and not a paint that failed");
        check(qAlpha(untitled.pixel(0, 0)) == 0,
              "QT CONTROL: with its corners still cut");
    }

    // --- THE DISCRIMINATOR: radius 0 --------------------------------------
    // Without this, a paint that cleared the corners for any reason at all -- a size mistake, an
    // off-by-one, an image smaller than the card -- would satisfy every corner assertion above.
    {
        tinta::TabGhostCard square = card;
        square.geometry.radius = 0.0f;
        QImage flat = sentinelImage(square.geometry);
        tinta::tabGhostPaint(flat, square, QStringLiteral("probe-document.md"));
        check(qAlpha(flat.pixel(0, 0)) == 255 &&
                  qAlpha(flat.pixel(square.geometry.width - 1, square.geometry.height - 1)) == 255,
              "QT DISCRIMINATOR: at radius 0 the corners are OPAQUE, so the transparent corners "
              "above are the rounding rule doing its job and not the paint missing the edges");
        check(flat.pixel(0, 0) != kSentinel,
              "QT DISCRIMINATOR: and they are painted rather than left as the sentinel");
    }

    // --- CONTROL: the fill is read from the card, not hardcoded ---------------
    {
        const tinta::Color other{0.60f, 0.20f, 0.10f, 1.0f};
        const tinta::TabGhostCard recoloured = tinta::tabGhostCard(1.0f, other, border, text);
        QImage second = sentinelImage(recoloured.geometry);
        tinta::tabGhostPaint(second, recoloured, QStringLiteral("probe-document.md"));
        const QRgb recentre = second.pixel(cx, cy);
        check(qRed(recentre) == 153 && qGreen(recentre) == 51 && qBlue(recentre) == 26,
              "QT CONTROL: a different fill colour produces a different centre pixel, so the arm "
              "reads the card rather than holding its own palette");
        check(recentre != centre,
              "QT CONTROL: and it is not the same pixel as the first card, so the two readings are "
              "not one reading twice");
    }

    // --- CONTROL: the geometry is read from the card too ---------------------
    {
        const tinta::TabGhostCard bigger = tinta::tabGhostCard(2.0f, fill, border, text);
        QImage large = sentinelImage(bigger.geometry);
        tinta::tabGhostPaint(large, bigger, QStringLiteral("probe-document.md"));
        check(large.width() == 380 && large.height() == 68,
              "QT CONTROL: the scale-2.0 card is 380x68");
        check(qAlpha(large.pixel(0, 0)) == 0 && qAlpha(large.pixel(190, 34)) == 255,
              "QT CONTROL: with its corners cut at the larger radius and its centre opaque, so the "
              "paint scales with the geometry instead of drawing a 190x34 card in a bigger image");
        // The far edges must be painted, which is the specific failure of a paint that used its own
        // idea of the size: the bottom-right would still be sentinel magenta.
        check(large.pixel(379, 34) != kSentinel && large.pixel(190, 67) != kSentinel,
              "QT CONTROL: and the far edges are not sentinel, so the whole image was covered");
    }

    // --- ISC-9 note: nothing above touched a native surface ------------------
    // Every reading in this file came out of a QImage. No HWND, no HDC, no DIB section, no
    // UpdateLayeredWindow -- and no Direct2D, which is the headline claim this seam contributes to.
    check(image.format() == QImage::Format_ARGB32_Premultiplied,
          "QT: and the surface every reading came from is a plain Qt ARGB32_Premultiplied image, "
          "with no native compositing anywhere on this arm");

    std::printf("%s: %d/%d assertions passed\n", failures == 0 ? "PASS" : "FAIL",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

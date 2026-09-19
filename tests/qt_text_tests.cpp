// The Qt 6 text-layout contract, measured rather than assumed.
//
// WHY THIS TARGET EXISTS
//
// Qt 6 is the chosen replacement for Direct2D/DirectWrite, and DirectWrite's text
// work is the largest single piece of that port: CreateTextFormat (37 sites),
// CreateTextLayout (36), GetMetrics (18), SetWordWrapping (8),
// HitTestTextPosition (6), HitTestTextRange (4), HitTestPoint (2),
// GetLineMetrics (3), CreateEllipsisTrimmingSign + SetTrimming (4) and
// CreateTypography + SetTypography (3).
//
// None of the backend exists yet. This file deliberately lands FIRST, because the
// thing worth having before any implementation is a working instrument -- and the
// obvious way to run Qt headlessly turns out to be a broken one:
//
//   QT_QPA_PLATFORM=minimal has NO FONT DATABASE AT ALL, and does not say so.
//   QFontDatabase::families() is empty, QFont("DejaVu Sans") resolves to an EMPTY
//   family name, and QFontInfo reports pointSize -0.8. Yet QTextLayout still
//   returns numbers and still exits 0. Measured side by side:
//
//     offscreen : 10 families, ascent 19.500  descent 4.953  advance 472.031
//     minimal   :  0 families, ascent 21.000  descent 0.000  advance 903.000
//
//   The minimal figures are fabricated, not wrong-by-a-little: every value is a
//   whole number, descent is exactly zero, and cursorToX(10) returns the full line
//   width. A harness that reached for "minimal" -- the natural choice for headless
//   testing -- would go green while measuring nothing.
//
// So this test REFUSES TO RUN unless a font database exists. That asserts the
// property directly rather than checking for a particular platform name, the same
// shape as the portable-mode guard in theme_data_tests.cpp. Note that running with
// no hint at all ABORTS (exit 134) in WSL, because the default xcb plugin cannot
// reach a display.
//
// CMake pins QT_QPA_PLATFORM PER PLATFORM -- offscreen off Windows, windows on it --
// because the font-database answer INVERTS: measured 2026-09-15, offscreen has fonts
// off Windows and NONE on it, where the windows plugin does. A single global pin that
// was correct on Linux made this guard refuse (exit 2) on the first Windows run. Two
// families are named per platform below for the same reason -- no one font family is
// present on all three targets, and asserting metrics against a silently substituted
// font measures a font nobody chose.
//
// One genuine Qt 6.4 GAP is recorded below rather than papered over: there is no
// QFont::setFeature, so DirectWrite's per-feature typography control has no direct
// equivalent. What tinta actually asks for is kerning, standard ligatures and
// contextual alternates for body text, plus discretionary ligatures for code. The
// first two are measured here as Qt defaults, so most of that gap is not real.

#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextOption>
#include <QString>

#include <cstdio>

namespace {

#ifdef _WIN32
constexpr bool kOnWindows = true;
#else
constexpr bool kOnWindows = false;
#endif

#ifdef __APPLE__
constexpr bool kOnMacOS = true;
#else
constexpr bool kOnMacOS = false;
#endif

// THE TOLERANCE FOR COMPARING A QTextLine WIDTH AGAINST A QFontMetricsF ADVANCE, and it
// has to be a whole pixel rather than the 0.01 this file used to demand. The two are
// DIFFERENT MEASUREMENT PATHS, not two readings of one: QTextLine accumulates subpixel
// positions, while QFontMetricsF::horizontalAdvance returns the font engine's HINTED
// advance -- integer-rounded on Windows. Measured on the paragraph below at 32pt, first
// wrapped line, natural width vs metrics advance of the same string:
//
//   Segoe UI  182.0000 vs 182.0000     Arial     188.0000 vs 188.0000
//   Calibri   169.0000 vs 169.0000     Cambria   183.2031 vs 183.0000  (0.203 apart)
//   Tahoma    184.8750 vs 184.0000  (0.875 apart)
//
// So 0.01 was an assertion that the two paths agree to the BIT, which was never the
// claim and passed on Linux only because DejaVu's advances happen to come out integral.
// The claim these assertions actually make is "the gap is one space wide, not zero" --
// and one space at this size is 10-13px, so a whole-pixel tolerance still fails loudly
// on the bug they exist to catch while surviving hinting on any font.
constexpr double kPathTolerance = 1.0;

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

void checkNear(double actual, double expected, double tolerance, const char* message) {
    check(actual > expected - tolerance && actual < expected + tolerance, message);
}

// Advance width via QFontMetricsF, which reaches the font directly and does not go
// through QTextLine. Used as the external anchor for the cursor-position assertions:
// a cursorToX/xToCursor round-trip only proves the two agree with each other.
double metricsOf(const QFont& font, const QString& text) {
    return QFontMetricsF(font).horizontalAdvance(text);
}

// Shape a string and return how many glyphs came out. Counting GLYPHS rather than
// comparing widths matters: a width change could come from kerning just as easily
// as from a ligature, so width cannot distinguish the two.
int shapedGlyphCount(const QFont& font, const QString& text) {
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (!line.isValid()) return -1;
    line.setLineWidth(100000.0);
    line.setPosition(QPointF(0, 0));
    layout.endLayout();
    int glyphs = 0;
    for (const QGlyphRun& run : layout.glyphRuns()) glyphs += run.glyphIndexes().size();
    return glyphs;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // --- The guard. See the header comment: this is not a nicety. --------------
    const QString platform = QGuiApplication::platformName();
    if (QFontDatabase::families().isEmpty()) {
        std::printf("FAIL: no font database under platform=%s, so every metric below\n"
                    "      would be fabricated rather than measured. Refusing to run.\n"
                    "      Use offscreen OFF Windows and windows ON it, never minimal.\n"
                    "      (offscreen has no font database on Windows -- measured\n"
                    "      2026-09-15, and the reason CMakeLists.txt pins this per\n"
                    "      platform rather than once.)\n",
                    platform.toUtf8().constData());
        return 2;
    }
    check(true, "a font database is present, so the metrics below are real");

    // A family the test can rely on resolving, NAMED PER PLATFORM because there is no
    // single family present on all of them. DejaVu ships with Qt's own fontconfig setup
    // on Linux; Segoe UI has shipped with every Windows since Vista; Helvetica has
    // shipped with every macOS release and resolved to itself when measured (Homebrew
    // Qt 6.11.2, macOS 26.6.2) -- DejaVu Sans is not installed there and silently
    // substitutes to the system UI font instead, which is exactly the un-asserted
    // substitution this control exists to catch.
    //
    // The requested name used to be "DejaVu Sans" unconditionally, and the only check on
    // it was that the resolved family was non-empty -- which is true of every
    // substitution, so it is not a control at all. Windows silently substituted TAHOMA
    // and four assertions below were measured against a font nobody had chosen. So the
    // resolution is now asserted EQUAL to the request, and printed either way.
    const char* const kPrimaryFamily =
        kOnWindows ? "Segoe UI" : (kOnMacOS ? "Helvetica" : "DejaVu Sans");
    QFont font(QString::fromLatin1(kPrimaryFamily), 32.0);
    const QFontInfo info(font);
    std::printf("      font requested=\"%s\" resolved=\"%s\" platform=%s\n",
                kPrimaryFamily, info.family().toUtf8().constData(),
                platform.toUtf8().constData());
    check(info.family() == QString::fromLatin1(kPrimaryFamily),
          "THE REQUESTED FAMILY RESOLVED TO ITSELF, not to a substitute -- every metric "
          "below is a property of this font and of no other");
    check(info.pointSizeF() > 0.0, "the resolved font has a positive point size");

    // --- CreateTextFormat -> QFont --------------------------------------------
    QFont bold = font;
    bold.setWeight(QFont::Bold);
    const double plainWidth = QFontMetricsF(font).horizontalAdvance("Heading");
    const double boldWidth = QFontMetricsF(bold).horizontalAdvance("Heading");
    check(boldWidth > plainWidth, "setWeight(Bold) actually changes advance width");

    // --- CreateTextLayout + SetWordWrapping + GetMetrics ----------------------
    const QString paragraph = "The quick brown fox jumps over the lazy dog";
    const double wrapWidth = 200.0;

    QTextLayout layout(paragraph, font);
    QTextOption option = layout.textOption();
    option.setWrapMode(QTextOption::WordWrap);
    layout.setTextOption(option);

    layout.beginLayout();
    double y = 0.0;
    int lineCount = 0;
    double widest = 0.0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(wrapWidth);
        line.setPosition(QPointF(0, y));
        y += line.height();
        if (line.naturalTextWidth() > widest) widest = line.naturalTextWidth();
        lineCount++;
    }
    layout.endLayout();

    check(lineCount == layout.lineCount(), "the loop's line count agrees with lineCount()");
    check(lineCount > 1, "a long paragraph wraps into more than one line at 200px");
    check(widest <= wrapWidth + 0.001, "no wrapped line exceeds the width it was given");

    const QTextLine first = layout.lineAt(0);
    check(first.isValid(), "lineAt(0) is valid -- this replaces GetLineMetrics");
    check(first.ascent() > 0.0, "the line reports a positive ascent");
    // descent > 0 is the discriminator against the minimal plugin, which reports
    // exactly 0.000 here. The guard above should already have caught that, so this
    // is a second, independent way for the same fault to surface.
    check(first.descent() > 0.0, "the line reports a NON-ZERO descent (minimal reports 0)");
    checkNear(first.height(), first.ascent() + first.descent(), 1.001,
              "height is ascent + descent to within a pixel of rounding");

    // Wrapping must not have lost or duplicated any of the text. This is anchored
    // on the ORIGINAL string length rather than on anything the layout reports, so
    // it cannot agree with a layout bug the way a self-comparison would.
    int covered = 0;
    for (int i = 0; i < layout.lineCount(); i++) covered += layout.lineAt(i).textLength();
    check(covered >= paragraph.size(),
          "the wrapped lines together cover the whole source string");

    // --- HitTestTextPosition / HitTestPoint -> cursorToX / xToCursor ----------
    //
    // A cursorToX/xToCursor round-trip compares the pair against each other and so
    // cannot see a fault they share. Both ends are therefore anchored on quantities
    // computed WITHOUT them: position 0 must sit at the line's own origin, and the
    // end of the line must sit at its independently-reported natural width.
    const double xAtStart = first.cursorToX(first.textStart());
    const double xAtEnd = first.cursorToX(first.textStart() + first.textLength());
    checkNear(xAtStart, 0.0, 0.001, "the first cursor position sits at x = 0");
    check(xAtEnd > xAtStart, "the end of the line is to the right of its start");

    // TRAILING WHITESPACE IS THE TRAP HERE, and this assertion was wrong before it
    // was measured. A wrapped line keeps the space it broke on inside textLength(),
    // and Qt treats that space inconsistently by design:
    //
    //   naturalTextWidth()   EXCLUDES it   (so does QTextLine::horizontalAdvance())
    //   cursorToX(end)       INCLUDES it
    //
    // Measured on line 0 of this very paragraph, which is "The ": natural width
    // 79.9688, cursorToX(end) 93.6250, difference 13.6562 -- exactly one space
    // advance, and cursorToX(end - 1) lands on 79.9688 to the digit. So a port that
    // placed the end-of-line caret at naturalTextWidth() would draw it one space too
    // far left on every wrapped line, which is a bug that looks like a rounding
    // error and is not one. Both figures are anchored on QFontMetricsF, a path that
    // does not go through QTextLine at all.
    const QString lineText = paragraph.mid(first.textStart(), first.textLength());
    check(lineText.endsWith(QLatin1Char(' ')),
          "the first wrapped line does end in the space it broke on");
    const double spaceAdvance = metricsOf(font, QStringLiteral(" "));
    check(spaceAdvance > kPathTolerance * 2.0,
          "CONTROL: one space is several pixels wide at this size, so kPathTolerance "
          "cannot swallow a whole space and the four assertions below still bite");
    checkNear(first.naturalTextWidth(), metricsOf(font, lineText.trimmed()), kPathTolerance,
              "naturalTextWidth EXCLUDES the trailing space");
    checkNear(xAtEnd, metricsOf(font, lineText), kPathTolerance,
              "cursorToX at the line end INCLUDES the trailing space");
    checkNear(xAtEnd - first.naturalTextWidth(), spaceAdvance, kPathTolerance,
              "the gap between them is one space advance");
    // Tolerance-free companion to the assertion above, and the one that would actually
    // catch the bug: whatever the hinting does to either path, the end-of-line caret must
    // sit most of a space to the RIGHT of the natural width. A port that placed it at
    // naturalTextWidth() lands at a gap of zero, which this rejects without depending on
    // the two measurement paths agreeing at all.
    check(xAtEnd - first.naturalTextWidth() > spaceAdvance * 0.5,
          "AND THE GAP IS REAL, NOT ROUNDING -- more than half a space wide");
    checkNear(first.cursorToX(first.textStart() + first.textLength() - 1),
              first.naturalTextWidth(), kPathTolerance,
              "the last NON-SPACE position is what lands on naturalTextWidth");

    const int cursorAtStart = first.xToCursor(0.0);
    check(cursorAtStart == first.textStart(), "x = 0 maps back to the first character");
    const int mid = first.xToCursor(first.naturalTextWidth() / 2.0);
    check(mid > first.textStart() && mid < first.textStart() + first.textLength(),
          "a point halfway along the line maps into the middle of it");

    // --- CreateEllipsisTrimmingSign + SetTrimming -> elidedText ---------------
    const QFontMetricsF metrics(font);
    const QString elided = metrics.elidedText(paragraph, Qt::ElideRight, 150.0);
    check(elided != paragraph, "elidedText actually shortens a string that does not fit");
    check(metrics.horizontalAdvance(elided) <= 150.0 + 0.001,
          "the elided string fits the width it was given");
    check(elided.endsWith(QChar(0x2026)) || elided.endsWith("..."),
          "the elided string ends in an ellipsis");
    check(metrics.elidedText("ok", Qt::ElideRight, 1000.0) == "ok",
          "a string that already fits is returned unchanged");

    // --- CreateTypography + SetTypography ------------------------------------
    //
    // Qt 6.4 has NO QFont::setFeature (it arrived in 6.7), so DirectWrite's
    // per-feature control has no direct equivalent. Measured below: two of the
    // features tinta asks for are already Qt defaults, which is why that gap is
    // mostly not real. NOT measured, and so not claimed: contextual alternates
    // (calt) and discretionary ligatures (dlig). dlig in particular has no Qt 6.4
    // API, and it is the one requested only for code fonts.
    QFont kerned = font;   kerned.setKerning(true);
    QFont unkerned = font; unkerned.setKerning(false);
    const double kernedWidth = QFontMetricsF(kerned).horizontalAdvance("AVATAR To Wa");
    const double unkernedWidth = QFontMetricsF(unkerned).horizontalAdvance("AVATAR To Wa");
    check(kernedWidth < unkernedWidth,
          "kerning is ON by default and setKerning(false) is an effective control");

    // THE LIGATURE ASSERTIONS NEED A FONT THAT HAS THE LIGATURE, which is a property of
    // the font and not of Qt -- so this family is named per platform too, and separately
    // from the primary one. Measured at 32pt under Qt 6.8.3 on Windows, glyph count for
    // "fi": Calibri 1; Tahoma, Segoe UI, Cambria, Times New Roman and Arial all 2. So
    // "ligatures are on by default" is a claim about Qt's shaper that can only be
    // demonstrated through a font whose GSUB actually offers the substitution, and
    // asserting it against a UI font measures the font's inventory instead. Measured on
    // macOS (Homebrew Qt 6.11.2): Helvetica gives glyph count 1 for "fi" (has the
    // ligature); Arial, Times New Roman, Georgia and Menlo all give 2 (no ligature) --
    // the same shape as the Windows measurement, a different font supplying it.
    const char* const kLigatureFamily =
        kOnWindows ? "Calibri" : (kOnMacOS ? "Helvetica" : "DejaVu Sans");
    QFont ligFont(QString::fromLatin1(kLigatureFamily), 32.0);
    check(QFontInfo(ligFont).family() == QString::fromLatin1(kLigatureFamily),
          "the ligature family resolved to itself, so the glyph counts below are its own");

    const int controlGlyphs = shapedGlyphCount(ligFont, "xy");
    check(controlGlyphs == 2,
          "CONTROL: an unligated pair shapes to 2 glyphs, so the counter can speak");
    const int ligatureGlyphs = shapedGlyphCount(ligFont, "fi");
    check(ligatureGlyphs == 1,
          "standard ligatures are ON by default -- 'fi' shapes to a single glyph");
    check(shapedGlyphCount(unkerned, "fi") >= 1, "shaping still works with kerning off");

    QFont unshaped = ligFont;
    unshaped.setStyleStrategy(QFont::PreferNoShaping);
    check(shapedGlyphCount(unshaped, "fi") == 2,
          "PreferNoShaping is the coarse Qt 6.4 lever for turning ligatures off");

    std::printf("%s%d qt-text assertions run, %d failed (platform=%s, Qt %s)\n",
                failures == 0 ? "all " : "", checks, failures,
                platform.toUtf8().constData(), qVersion());
    return failures == 0 ? 0 : 1;
}

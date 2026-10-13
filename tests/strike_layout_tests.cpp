// ~~strike~~ lines cross the text they strike. The line used to sit at a fixed 0.55 of the
// 1.7x line slot, while the text draws at the top of the slot at its own height, so it
// landed on the baseline and read as an underline. Each strike line is checked against the
// run it crosses: a third of the ascent above that run's baseline, which is where Qt draws
// a font's own strike-out -- measured here by drawing one, not assumed.
#include "qt_test_app.h"
#include "theme_apply.h"
#include "render.h"
#include "text_qt.h"

#include <QImage>
#include <QPainter>
#include <QTextLayout>

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

struct Strike {
    float y, left, right;
    const App::LayoutTextRun* run;  // the text run the line crosses; null over math
    float baseline;                 // that run's
};

// A strike line is horizontal, opaque and crosses the glyph band of a run under it. Link
// dashes are translucent, and rules sit outside any run's glyph band.
std::vector<Strike> strikes(const App& app) {
    std::vector<Strike> out;
    for (const auto& line : app.layoutLines) {
        if (line.p1.y != line.p2.y || line.color.a < 0.99f) continue;
        float left = std::min(line.p1.x, line.p2.x), right = std::max(line.p1.x, line.p2.x);
        for (const auto& run : app.layoutTextRuns) {
            if (!run.layout || run.docLength == 0) continue;
            if (run.pos.x > left + 1 || run.pos.x + 1 < left) continue;
            tinta::LineMetrics metrics{};
            uint32_t count = 1;
            if (!run.layout->GetLineMetrics(&metrics, 1, &count)) continue;
            if (line.p1.y < run.pos.y || line.p1.y > run.pos.y + metrics.height) continue;
            out.push_back({line.p1.y, left, right, &run, metrics.baseline});
            break;
        }
    }
    return out;
}

void layout(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "strike source parses");
    app.root = doc.root;
    layoutDocument(app);
}

// Qt's own strike-out for `format`, in pixels below the top of the drawn text: spaces carry
// no ink, so the only dark rows are the decoration's.
float nativeStrikeRow(tinta::TextFormat* format) {
    QFont font = static_cast<QtTextFormat*>(format)->font();
    font.setStrikeOut(true);
    QTextLayout probe(QStringLiteral("          "), font);
    probe.beginLayout();
    QTextLine line = probe.createLine();
    probe.endLayout();
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::black);
        probe.draw(&painter, QPointF(0, 0));
    }
    float sum = 0, weight = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            float ink = 1.0f - qGray(image.pixel(x, y)) / 255.0f;
            sum += ink * (y + 0.5f);
            weight += ink;
        }
    }
    // A platform plugin with no font database draws every glyph as a box with no descent,
    // whose decoration says nothing about the real face (Windows' offscreen plugin does)
    check(line.descent() > 0, "the strike-out probe drew a real face");
    return weight > 0 ? sum / weight : -1.0f;
}

void checkPlacement(const std::vector<Strike>& found, const char* what) {
    for (const auto& s : found) {
        if (!s.run) continue;
        float drop = s.y - s.run->pos.y;
        if (drop < s.baseline * 0.55f || drop > s.baseline * 0.8f)
            std::cerr << what << ": strike at " << drop << " below the text top, baseline "
                      << s.baseline << '\n';
        check(drop > s.baseline * 0.55f, "strike line is below the top half of the ascent");
        check(drop < s.baseline * 0.8f, "strike line is well above the baseline");
    }
}
}

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!QtTestApp::install(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        std::ifstream file(TINTA_STRIKE_FIXTURE, std::ios::binary);
        check(file.good(), "strike fixture exists");
        const std::string fixture{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);

                // The line is where Qt would draw the font's own strike-out
                app.width = 1050;
                layout(app, "Plain ~~struck words~~ text.\n");
                auto plain = strikes(app);
                check(plain.size() == 1 && plain[0].run, "one strike line over one struck run");
                float native = nativeStrikeRow(app.textFormat);
                check(native > 0, "Qt drew a strike-out to compare with");
                if (plain.size() == 1 && plain[0].run && native > 0) {
                    float drop = plain[0].y - plain[0].run->pos.y;
                    if (std::abs(drop - native) > 1.0f)
                        std::cerr << "strike at " << drop << ", Qt's at " << native << '\n';
                    check(std::abs(drop - native) <= 1.0f, "strike line is Qt's own strike-out row");
                }

                for (int width : {1050, 650}) {
                    app.width = width;
                    layout(app, "Plain ~~struck words~~ text.\n");
                    checkPlacement(strikes(app), "plain");

                    layout(app, "# Heading with ~~struck~~ words\n\nBody ~~*italic*~~ and "
                                "~~**bold**~~ and ~~x^2^ sup~~.\n");
                    auto mixed = strikes(app);
                    check(mixed.size() >= 4, "heading, italic, bold and sup strikes are drawn");
                    checkPlacement(mixed, "mixed");

                    // A code span inside a strike is struck across its own glyphs
                    layout(app, "Before ~~`struck code`~~ after.\n");
                    bool codeStruck = false;
                    for (const auto& run : app.layoutTextRuns) {
                        if (!run.layout || run.docLength == 0) continue;
                        if (app.docText.compare(run.docStart, run.docLength, L"struck code") != 0) continue;
                        for (const auto& s : strikes(app)) {
                            if (s.run == &run) codeStruck = true;
                        }
                    }
                    check(codeStruck, "inline code inside ~~ ~~ is struck");
                    checkPlacement(strikes(app), "code");

                    layout(app, "~~A strike long enough to wrap onto a second line at every width "
                                "this test lays out, so each wrapped line carries its own line, "
                                "and every one of them crosses its own text.~~\n");
                    auto wrapped = strikes(app);
                    bool twoRows = wrapped.size() >= 2 && wrapped.back().y > wrapped.front().y + 10;
                    check(twoRows, "a wrapped strike is drawn on each line");
                    checkPlacement(wrapped, "wrapped");

                    layout(app, fixture);
                    auto all = strikes(app);
                    // 15 struck spans outside code, each drawn as at least one line
                    if (all.size() < 15) std::cerr << "fixture strikes: " << all.size() << '\n';
                    check(all.size() >= 15, "the fixture's strikes are found");
                    checkPlacement(all, "fixture");
                    check(app.docText.find(L"~~*literal*~~") != std::wstring::npos,
                          "strike syntax inside code stays literal");
                }
            }
        }
        releaseQtBackends(app);
    }
    std::cout << "Strike layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}

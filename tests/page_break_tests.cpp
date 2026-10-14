// Page breaks do not cut a code block or a table row. A break was pulled above any text line
// it would cut, but a code block's box and a table row's frame are not lines: the break landed
// between the box top and its first line, and the printed page ended in an empty grey sliver
// (or an empty table row) with the content on the next page.
//
// The sweep moves a code block and a table down the page one filler paragraph at a time, so
// the natural page foot falls inside each of them for some count. AT RISK counts those cases
// from the geometry alone -- without it a green would be a sweep that never hit the defect.
#include "qt_test_app.h"
#include "theme_apply.h"
#include "render.h"
#include "print.h"

#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!QtTestApp::install(app)) return 2;
        applyTheme(app, 0);
        app.contentScale = 1.0f;
        updateTextFormats(app);
        app.width = 700;
        app.height = 4000;
        app.readingWidthPct = 100;

        const float pageH = 1000.0f;
        int atRisk = 0, cut = 0, layouts = 0;
        for (int filler = 0; filler < 60; ++filler) {
            std::string source = "# Page breaks\n\n";
            for (int i = 0; i < filler; ++i) source += "A filler line of ordinary text.\n\n";
            source += "```\n";
            for (int i = 0; i < 8; ++i) source += "code line " + std::to_string(i) + "\n";
            source += "```\n\n| A | B |\n| --- | --- |\n";
            for (int i = 0; i < 6; ++i) source += "| row " + std::to_string(i) + " | cell |\n";
            source += "\nAfter the table.\n";

            auto doc = app.parser.parse(source);
            app.root = doc.root;
            app.layoutDirty = true;
            layoutDocument(app);
            ++layouts;

            for (const auto& block : app.keepTogetherRects) {
                if (block.top < pageH && block.bottom > pageH) ++atRisk;
            }
            const std::vector<float> breaks = computePageBreaks(app, pageH);
            for (float at : breaks) {
                for (const auto& block : app.keepTogetherRects) {
                    // An atom taller than most of a page is sliced on purpose
                    if (block.bottom - block.top > pageH * 0.7f) continue;
                    if (block.top < at - 0.5f && block.bottom > at + 0.5f) {
                        ++cut;
                        std::cerr << "filler " << filler << ": break at " << at
                                  << " cuts a block " << block.top << ".." << block.bottom << '\n';
                    }
                }
            }
        }
        std::cout << "page_breaks layouts=" << layouts << " at_risk=" << atRisk
                  << " cut=" << cut << '\n';
        check(app.keepTogetherRects.size() >= 7,
              "the layout records the code block and every table row to keep together");
        check(atRisk >= 5,
              "CONTROL: the sweep puts the natural page foot inside a block several times");
        check(cut == 0, "no page break cuts a code block or a table row");
        releaseQtBackends(app);
    }
    std::cout << "Page breaks: " << failures << " failures\n";
    return failures ? 1 : 0;
}

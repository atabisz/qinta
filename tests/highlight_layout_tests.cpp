// ==highlight== backgrounds hug the glyphs they mark. The marker used to fill the whole
// 1.7x line slot, so it hung about half a font-size of colour below the descenders.
// Each marker is checked against the text run drawn over it: same top, the run's own
// glyph-line height, and clear of the slot's foot.
#include "qt_test_app.h"
#include "theme_apply.h"
#include "render.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b, float tolerance = 0.5f) { return std::abs(a-b) < tolerance; }

// The marker colours from inline_style.cpp: no other inline background is this yellow
bool isMarker(const App& app, tinta::Color c) {
    const float alpha = app.theme.isDark ? 0.28f : 0.45f;
    return c.r > 0.95f && c.g > 0.75f && c.b < 0.3f && closeEnough(c.a, alpha, 0.01f);
}

size_t layout(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "highlight fixture parses");
    app.root = doc.root;
    layoutDocument(app);
    size_t highlights = 0;
    std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& e) {
        if (!e) return;
        if (e->type == qmd::ElementType::Highlight) ++highlights;
        for (const auto& child : e->children) visit(child);
    };
    if (doc.success) visit(doc.root);
    return highlights;
}

// Every marker over text matches that text's glyph box. A marker over inline math is
// left alone: it grows to the math box on purpose. Returns the marker count.
size_t checkMarkers(const App& app) {
    size_t markers = 0;
    for (const auto& item : app.layoutRects) {
        if (!isMarker(app, item.color)) continue;
        ++markers;
        const auto& r = item.rect;
        const App::LayoutTextRun* under = nullptr;
        for (const auto& run : app.layoutTextRuns) {
            if (!run.layout || run.pos.x < r.left || run.pos.x > r.left + 4) continue;
            if (run.bounds.top > r.top + 1 || run.bounds.bottom < r.bottom - 1) continue;
            under = &run;
            break;
        }
        check(under != nullptr, "every marker covers a run");
        if (!under || under->docLength == 0) continue;
        tinta::LineMetrics metrics{};
        uint32_t count = 1;
        check(under->layout->GetLineMetrics(&metrics, 1, &count), "marked run has line metrics");
        check(closeEnough(r.top, under->pos.y), "marker top is the glyph top");
        check(closeEnough(r.bottom - r.top, metrics.height), "marker is the glyph line's height");
        check(r.bottom > under->pos.y + metrics.baseline, "marker reaches below the baseline");
        check(under->bounds.bottom - r.bottom > metrics.height * 0.2f,
              "marker stops short of the line slot's foot");
    }
    return markers;
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
        std::ifstream file(TINTA_HIGHLIGHT_FIXTURE, std::ios::binary);
        check(file.good(), "highlight fixture exists");
        const std::string fixture{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650}) {
                    app.width = width;
                    layout(app, "Plain ==highlight== text.\n");
                    check(checkMarkers(app) == 1, "one marker for one highlight");

                    layout(app, "==A highlight long enough to wrap onto a second line at every "
                                "width this test lays out, so each wrapped line carries its own "
                                "marker, and every one of them is as tall as its text and no "
                                "taller than the glyphs it sits behind.==\n");
                    check(checkMarkers(app) >= 2, "a wrapped highlight is marked on each line");

                    const size_t highlights = layout(app, fixture);
                    check(highlights >= 15, "fixture highlights parse");
                    check(checkMarkers(app) >= highlights, "every fixture highlight is marked");
                    check(app.docText.find(L"==not a highlight inside code==") != std::wstring::npos,
                          "highlight syntax inside code stays literal");
                    check(app.docText.find(L"Closing paragraph") != std::wstring::npos,
                          "final mixed content renders");
                }
            }
        }
        releaseQtBackends(app);
    }
    std::cout << "Highlight layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}

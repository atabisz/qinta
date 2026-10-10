// Raw HTML blocks as a README writes them: <div align="center"> around a heading and a
// tagline, and an inline <a> inside a sentence. Three regressions are pinned here: the
// space before an inline tag was trimmed away ("fork ofTinta"), align= was ignored, and
// every link carried a solid underline at the foot of its 1.7x line box.
#include "qt_test_app.h"
#include "theme_apply.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void layout(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "fixture parses");
    app.root = doc.root;
    layoutDocument(app);
}

struct Row { float left, right; };

// Every laid-out line covering docText[from, to), keyed by its top edge
std::map<float, Row> rowsFor(const App& app, const std::wstring& from, const std::wstring& to) {
    std::map<float, Row> rows;
    size_t start = app.docText.find(from);
    if (start == std::wstring::npos) return rows;
    size_t end = app.docText.find(to, start);
    if (end == std::wstring::npos) return rows;
    end += to.size();
    for (const auto& r : app.textRects) {
        if (r.docStart < start || r.docStart >= end) continue;
        auto it = rows.find(r.rect.top);
        if (it == rows.end()) rows[r.rect.top] = {r.rect.left, r.rect.right};
        else {
            it->second.left = std::min(it->second.left, r.rect.left);
            it->second.right = std::max(it->second.right, r.rect.right);
        }
    }
    return rows;
}

void fixtureCases(App& app, const std::string& source, const std::string& where) {
    layout(app, source);

    // The content box, read off the H1 rule rather than recomputed
    float boxLeft = 0, boxRight = 0;
    for (const auto& line : app.layoutLines)
        if (line.p2.x - line.p1.x > boxRight - boxLeft) { boxLeft = line.p1.x; boxRight = line.p2.x; }
    check(boxRight > boxLeft, where + ": heading rule found");

    check(app.docText.find(L"fork of Tinta, moving") != std::wstring::npos,
          where + ": the space before an inline <a> survives");
    check(app.docText.find(L"text bold italic and a link keep") != std::wstring::npos,
          where + ": spaces around inline <b>, <i> and <a> survive");

    auto centered = [&](const std::wstring& from, const std::wstring& to, const std::string& what,
                        size_t minRows) {
        auto rows = rowsFor(app, from, to);
        check(rows.size() >= minRows, where + ": " + what + " laid out");
        for (const auto& [top, row] : rows) {
            float leftGap = row.left - boxLeft, rightGap = boxRight - row.right;
            check(std::abs(leftGap - rightGap) < 1.5f,
                  where + ": " + what + " line centered (gaps " + std::to_string(leftGap) +
                  " / " + std::to_string(rightGap) + ")");
        }
    };
    centered(L"Centered heading", L"Centered heading", "heading", 1);
    centered(L"An italic tagline", L"heading.", "tagline", 1);
    centered(L"A multiplatform fork", L"Qt 6", "link sentence", 1);
    centered(L"A longer centered", L"widest line.", "wrapped paragraph", 2);

    auto right = rowsFor(app, L"A right-aligned", L"link.");
    check(right.size() == 1, where + ": right-aligned paragraph laid out");
    for (const auto& [top, row] : right)
        check(std::abs(row.right - boxRight) < 1.5f, where + ": right-aligned paragraph ends at the box edge");

    auto plain = rowsFor(app, L"Plain Markdown", L"Plain Markdown");
    check(plain.size() == 1 && std::abs(plain.begin()->second.left - boxLeft) < 0.5f,
          where + ": Markdown after the HTML block stays left aligned");

    check(!app.linkRects.empty(), where + ": links recorded");
    int underlined = 0;
    for (const auto& link : app.linkRects)
        for (const auto& line : app.layoutLines) {
            float y = line.p1.y;
            if (y >= link.bounds.top && y <= link.bounds.bottom &&
                line.p1.x >= link.bounds.left - 0.5f && line.p2.x <= link.bounds.right + 0.5f)
                underlined++;
        }
    check(underlined == 0, where + ": plain links draw no underline (" + std::to_string(underlined) + ")");
    check(app.docText.find(L"All surrounding content") != std::wstring::npos, where + ": final content renders");
}
}

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;
    std::ifstream file(TINTA_HTML_FIXTURE, std::ios::binary);
    check(file.good(), "fixture exists");
    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!QtTestApp::install(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650}) {
                    app.width = width;
                    fixtureCases(app, source, "theme " + std::to_string(theme) + " scale " +
                                 std::to_string(scale) + " width " + std::to_string(width));
                }
            }
        }
        releaseQtBackends(app);
    }
    std::cout << "HTML layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}

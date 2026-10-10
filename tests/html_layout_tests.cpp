// Raw HTML blocks as a README writes them: <div align="center"> around a heading and a
// tagline, and an inline <a> inside a sentence. Three regressions are pinned here: the
// space before an inline tag was trimmed away ("fork ofTinta"), align= was ignored, and
// every link carried a solid underline at the foot of its 1.7x line box. Then <img> and the
// inline formatting tags: an <img> in an HTML block was dropped outright, and inline HTML in
// a Markdown paragraph (<sub>, <sup>, <mark>, ...) was drawn as its literal source.
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

// The first element of `type` in a parsed tree, depth first
const Element* findType(const ElementPtr& e, ElementType type) {
    if (!e) return nullptr;
    if (e->type == type) return e.get();
    for (const auto& c : e->children)
        if (const Element* found = findType(c, type)) return found;
    return nullptr;
}

void parserCases(App& app) {
    for (const char* source : {"H<sub>2</sub>O\n", "<p>H<sub>2</sub>O</p>\n"}) {
        auto doc = app.parser.parse(source);
        const Element* sub = findType(doc.root, ElementType::Subscript);
        check(sub && sub->children.size() == 1 && sub->children[0]->text == "2",
              std::string("<sub> becomes a Subscript element: ") + source);
    }
    for (auto [tag, type] : {std::pair{"sup", ElementType::Superscript},
                             std::pair{"mark", ElementType::Highlight},
                             std::pair{"del", ElementType::Strikethrough},
                             std::pair{"s", ElementType::Strikethrough},
                             std::pair{"b", ElementType::Strong},
                             std::pair{"em", ElementType::Emphasis}}) {
        std::string source = std::string("a <") + tag + ">x</" + tag + "> b\n";
        auto doc = app.parser.parse(source);
        check(findType(doc.root, type) != nullptr, "inline <" + std::string(tag) + "> maps");
    }
    // Each parse result is held: findType returns a pointer into its tree
    auto imgDoc = app.parser.parse("<div align=\"center\">\n  <img src=\"a.png\" width=80 height=\"40px\" alt=\"Logo\">\n</div>\n");
    const Element* img = findType(imgDoc.root, ElementType::Image);
    check(img && img->url == "a.png" && img->width == 80 && img->height == 40,
          "<img> in an HTML block keeps src and bare and px sizes");
    check(img && img->parent && img->parent->type == ElementType::Paragraph && img->parent->align == 2,
          "an <img> directly in an aligned div is wrapped in an aligned paragraph");
    check(img && !img->children.empty() && img->children[0]->text == "Logo", "<img> alt becomes its text");
    auto inlineDoc = app.parser.parse("See <img src=\"b.png\"> here\n");
    const Element* inlineImg = findType(inlineDoc.root, ElementType::Image);
    check(inlineImg && inlineImg->url == "b.png", "inline <img> in a Markdown paragraph becomes an Image");
    auto valignDoc = app.parser.parse("<div valign=\"center\"><p>x</p></div>\n");
    const Element* valignP = findType(valignDoc.root, ElementType::Paragraph);
    check(valignP && valignP->align == 0, "valign= is not read as align=");
    auto stray = app.parser.parse("a </sub> b <sub>c\n");
    check(findType(stray.root, ElementType::Paragraph) != nullptr, "stray and unclosed inline tags still parse");
}

// A paragraph that is only an image ends on the image: the next block starts the image's
// own 12px margin plus the 14px paragraph gap below it, with no empty text line between
void imageGapCases(App& app, const std::string& where) {
    const float scale = app.contentScale * app.zoomFactor;
    for (const char* source : {"![badge](svg-image-sample.svg)\n\nNext paragraph.\n",
                               "<p><img src=\"svg-image-sample.svg\"></p>\n\nNext paragraph.\n"}) {
        layout(app, source);
        float next = -1;
        for (const auto& run : app.layoutTextRuns)
            if (run.docLength && app.docText.substr(run.docStart, run.docLength) == L"Next paragraph.")
                next = run.bounds.top;
        check(app.layoutBitmaps.size() == 1 && next > 0, where + ": image and next paragraph laid out");
        if (app.layoutBitmaps.size() == 1 && next > 0) {
            float gap = next - app.layoutBitmaps[0].destRect.bottom;
            check(std::abs(gap - 26 * scale) < 0.5f, where + ": image-only paragraph adds no empty line (gap " +
                  std::to_string(gap) + ") " + source);
        }
    }
}

void fixtureCases(App& app, const std::string& source, const std::string& where) {
    layout(app, source);
    const float scale = app.contentScale * app.zoomFactor;

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
    centered(L"Markdown heading in", L"centered div", "Markdown heading inside an aligned div", 1);
    centered(L"A Markdown paragraph centered", L"a README.", "Markdown paragraph inside an aligned div", 1);

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

    check(app.docText.find(L"<sub>") == std::wstring::npos && app.docText.find(L"<sup>") == std::wstring::npos &&
          app.docText.find(L"<mark>") == std::wstring::npos && app.docText.find(L"<del>") == std::wstring::npos,
          where + ": no inline tag is drawn as literal text");
    check(app.docText.find(L"CO2, E = mc2, highlighted") != std::wstring::npos,
          where + ": inline HTML in Markdown keeps its text and spacing");
    check(app.docText.find(L"Water is H2O, area is m2") != std::wstring::npos,
          where + ": <sub>/<sup> in an HTML block keep their text");

    // The logo (width=80) and the badge (height=28) load and are centered by their own width
    check(app.layoutBitmaps.size() == 2, where + ": both local images load (" +
          std::to_string(app.layoutBitmaps.size()) + ")");
    if (app.layoutBitmaps.size() == 2) {
        const auto& logo = app.layoutBitmaps[0].destRect;
        check(std::abs((logo.right - logo.left) - 80 * scale) < 0.5f, where + ": width=80 is honoured");
        const auto& badge = app.layoutBitmaps[1].destRect;
        check(std::abs((badge.bottom - badge.top) - 28 * scale) < 0.5f, where + ": height=28 is honoured");
        for (const auto* r : {&logo, &badge})
            check(std::abs((r->left - boxLeft) - (boxRight - r->right)) < 1.5f, where + ": image centered");
        bool badgeLinked = false;
        for (const auto& link : app.linkRects)
            if (link.url == "https://example.com/badge" && link.bounds.left == badge.left &&
                link.bounds.top == badge.top) badgeLinked = true;
        check(badgeLinked, where + ": a linked image is clickable where it is drawn");
    }
    bool placeholderCentered = false;
    for (const auto& run : app.layoutTextRuns)
        if (run.docLength && app.docText.substr(run.docStart, run.docLength) == L"[image: a missing image]")
            placeholderCentered = std::abs((run.bounds.left - boxLeft) - (boxRight - run.bounds.right)) < 1.5f;
    check(placeholderCentered, where + ": a missing image's placeholder is centered");
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
        app.currentFile = TINTA_HTML_FIXTURE;  // images resolve beside the document
        parserCases(app);
        app.height = 900;
        app.readingWidthPct = 100;
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650}) {
                    app.width = width;
                    const std::string where = "theme " + std::to_string(theme) + " scale " +
                                              std::to_string(scale) + " width " + std::to_string(width);
                    fixtureCases(app, source, where);
                    imageGapCases(app, where);
                }
            }
        }
        releaseQtBackends(app);
    }
    std::cout << "HTML layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}

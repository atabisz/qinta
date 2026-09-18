#include "chrome_dump.h"

#include <dwrite.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "app.h"
#include "d2d_init.h"
#include "text_d2d.h"
#include "text_utils.h"
#include "theme_data.h"

// Windows-only by construction: every read below is a DirectWrite getter. The
// declaration in include/chrome_dump.h says why the reading has to happen down here
// rather than through the seam, and what it deliberately cannot see.

namespace {

// One row's worth of properties, in a fixed order, as ASCII. Every field is printed for
// every format even where the value is the DirectWrite default -- a dump that omits
// defaults cannot tell "unchanged" from "no longer set", which is the exact distinction
// claim 18 asks an unchanged reading to make.
void writeRow(std::ofstream& out, const char* slot, IDWriteTextFormat* format) {
    if (!format) {
        // NOT silence, and not a skipped line. A missing format is the most interesting
        // thing this dump can find -- it is the shape that segfaulted render.cpp once for
        // the document formats -- so it gets a row of its own and moves the diff.
        out << slot << " MISSING\n";
        return;
    }

    std::wstring family(format->GetFontFamilyNameLength() + 1, L'\0');
    if (FAILED(format->GetFontFamilyName(family.data(), (UINT32)family.size()))) {
        family = L"<unreadable>";
    }
    while (!family.empty() && family.back() == L'\0') family.pop_back();

    std::wstring locale(format->GetLocaleNameLength() + 1, L'\0');
    if (FAILED(format->GetLocaleName(locale.data(), (UINT32)locale.size()))) {
        locale = L"<unreadable>";
    }
    while (!locale.empty() && locale.back() == L'\0') locale.pop_back();

    DWRITE_TRIMMING trimming{};
    IDWriteInlineObject* sign = nullptr;
    const bool trimmingRead = SUCCEEDED(format->GetTrimming(&trimming, &sign));
    // GetTrimming ADDREFS the sign. Releasing it here is correct and does not free the
    // app's copy: the format holds its own reference (which is exactly why
    // App::trimmingSigns exists on the Qt side and not this one -- see include/app.h).
    const bool hasSign = sign != nullptr;
    if (sign) sign->Release();

    DWRITE_LINE_SPACING_METHOD spacingMethod = DWRITE_LINE_SPACING_METHOD_DEFAULT;
    FLOAT lineHeight = 0.0f;
    FLOAT baseline = 0.0f;
    const bool spacingRead =
        SUCCEEDED(format->GetLineSpacing(&spacingMethod, &lineHeight, &baseline));

    out << slot << " family=\"" << toUtf8(family) << "\""
        << " size=" << format->GetFontSize() << " weight=" << (int)format->GetFontWeight()
        << " style=" << (int)format->GetFontStyle()
        << " stretch=" << (int)format->GetFontStretch() << " locale=\"" << toUtf8(locale)
        << "\"" << " wrap=" << (int)format->GetWordWrapping()
        << " talign=" << (int)format->GetTextAlignment()
        << " palign=" << (int)format->GetParagraphAlignment();

    if (trimmingRead) {
        out << " trim=" << (int)trimming.granularity << "," << (int)trimming.delimiter << ","
            << (int)trimming.delimiterCount << " trimsign=" << (hasSign ? "yes" : "no");
    } else {
        out << " trim=UNREADABLE trimsign=UNREADABLE";
    }

    if (spacingRead) {
        out << " spacing=" << (int)spacingMethod << "," << lineHeight << "," << baseline;
    } else {
        out << " spacing=UNREADABLE";
    }

    out << "\n";
}

// Reads the NATIVE object behind a seam pointer. This one indirection is the whole
// reason the dump survives the port -- see include/chrome_dump.h.
void writeSeamRow(std::ofstream& out, const char* slot, tinta::TextFormat* format) {
    writeRow(out, slot, format ? nativeTextFormat(format) : nullptr);
}

}  // namespace

int dumpChromeFormats(App& app, const std::wstring& path) {
    std::ofstream out(path);
    if (!out) return 0;

    // THE ARM'S OWN CONTEXT, first, because every family literal and every size in the
    // rows below is a function of these four values. Two dumps that disagree because they
    // were taken at different scales would otherwise read as a code change -- which is
    // memory baseline-must-name-its-build-dir applied to a different instrument.
    const D2DTheme& theme = themeAt(app.currentThemeIndex);
    out << "arm theme_index=" << app.currentThemeIndex << " theme=\"" << toUtf8(theme.name)
        << "\"" << " content_scale=" << app.contentScale << " zoom=" << app.zoomFactor
        << " theme_count=" << themeCount() << "\n";

    int rows = 0;
    const auto row = [&](const char* slot, tinta::TextFormat* format) {
        writeSeamRow(out, slot, format);
        rows++;
    };

    // The ten flat formats, in updateOverlayFormats' own creation order so a reader can
    // hold the dump beside the function.
    row("search", app.searchTextFormat);
    row("signal_small", app.signalSmallFormat);
    row("theme_title", app.themeTitleFormat);
    row("theme_header", app.themeHeaderFormat);
    row("stats", app.statsFormat);
    row("folder_browser", app.folderBrowserFormat);
    row("toc_bold", app.tocFormatBold);
    row("toc", app.tocFormat);
    row("editor_text", app.editorTextFormat);
    row("editor_gutter", app.editorGutterFormat);

    // The per-theme triples. The COUNT is printed as well as the rows, because an empty
    // vector here is the lazy door still shut (ensureThemePreviewFormats early-returns on
    // non-empty) and thirty absent rows would otherwise read as thirty unchanged ones --
    // an empty reading is not a negative reading (Algorithm claim 24).
    out << "previews seated=" << app.themePreviewFormats.size() << "\n";
    for (size_t i = 0; i < app.themePreviewFormats.size(); i++) {
        const App::ThemePreviewFormats& slot = app.themePreviewFormats[i];
        char label[64];
        std::snprintf(label, sizeof(label), "preview[%zu].name", i);
        row(label, slot.name);
        std::snprintf(label, sizeof(label), "preview[%zu].preview", i);
        row(label, slot.preview);
        std::snprintf(label, sizeof(label), "preview[%zu].code", i);
        row(label, slot.code);
    }

    out << "rows " << rows << "\n";
    out.flush();
    return out ? rows : 0;
}

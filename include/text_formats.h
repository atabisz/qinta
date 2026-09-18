#ifndef TINTA_TEXT_FORMATS_H
#define TINTA_TEXT_FORMATS_H

// The sixteen cached DOCUMENT text formats, created through the seam and owned by App.
//
// WHY THIS HEADER EXISTS: the same eleven declarations were being built TWICE, once per
// backend, and the two copies were line-for-line parallel with one of them naming
// DirectWrite. src/d2d_init.cpp's updateTextFormats called
// dwriteFactory->CreateTextFormat eleven times into App::native*Format and then seated a
// wrapper over each; src/qt_init.cpp's installQtTextFormats called
// textFactory->CreateTextFormat with the same families, sizes and weights and installed
// the result. That file's own comment already said what the duplication cost -- "a number
// that disagrees with d2d_init is a divergence, not a Qt decision" -- which is a warning
// about a shape, not a fix for it. There is now one copy of the numbers and both
// initialisers call it.
//
// THE ELEVEN, WHICH IS SIXTEEN OBJECTS: textFormat, headingFormat, codeFormat,
// boldFormat, italicFormat, boldItalicFormat, codeBoldFormat, codeItalicFormat,
// codeBoldItalicFormat, supSubFormat and headingFormats[6]. Every bold/italic
// combination exists because inline spans nest -- ***both***, **`code`**, *`code`*.
//
// NOT THE CHROME FORMATS, and the line is drawn by which function creates them on
// Windows rather than by what they look like: these are what updateTextFormats builds,
// the ~20 others are updateOverlayFormats and ensureThemePreviewFormats, and those two
// still create theirs natively. That list read THREE until 2026-09-18, the third being
// createTypography -- which is now installTypography below, in this header and the
// portable file, because it never named DirectWrite in the first place. That is why App still
// carries Seated<> storages next to the plain unique_ptrs these use -- a mixed shape that
// is temporary by construction, since the second kind disappears as each of the other
// three functions follows this one.
//
// PORTABLE: nothing here or in src/text_formats_app.cpp names a Windows type, which is
// what lets a non-Windows build create a document format at all. Before this existed, off
// Windows App had a text FACTORY and no text FORMATS -- the first document handed to a Qt
// window segfaulted on a null baseFormat -- and the fix lived in a Qt-only file, so the
// portability was real and unshared.

#include "text_api.h"

struct App;

// THE TWO TYPOGRAPHY SETS, AS FUNCTIONS RATHER THAN AS A TABLE INSIDE AN App-TAKING ONE.
//
// They live in this header, and not in the .cpp beside installDocumentTextFormats, for one
// measured reason: tests/typography_tests.cpp is HEADER-ONLY -- it links no source file at
// all -- and it used to build its own local copies of both sets. Its comment said why, and
// named two blockers: "Reproduced here rather than called, because that function takes an
// App& and lives in a Windows-only file." Moving the file expires the second blocker and
// not the first, so the sets themselves have to stop taking an App& before that test can
// assert the real thing. Now it calls these, and a plant in either set fails it. Before, a
// plant in createTypography passed the test that exists to guard it.
//
// The tags are the four-character OpenType names, and the divergence between the backends
// is documented once at tinta::qtCanHonourFeature in text_api.h -- including that dropping
// `dlig` on Qt is a decision (Alex, 2026-09-15) rather than an omission.
namespace tinta {

// Body font: standard ligatures, kerning, contextual alternates. Qt honours all three.
inline Typography bodyTypographySet() {
    Typography t;
    t.add("liga");
    t.add("kern");
    t.add("calt");
    return t;
}

// Code font: programming ligatures, for faces like Cascadia Code and Fira Code.
inline Typography codeTypographySet() {
    Typography t;
    t.add("liga");
    t.add("dlig");
    return t;
}

}  // namespace tinta

// Assigns both sets into App. One call, at startup, on BOTH stacks -- and the second of
// those is new: createTypography, which this replaces, had exactly one caller in the tree
// (src/main_d2d.cpp), so the Qt path ran with two default-constructed sets and applied no
// OpenType feature list to any of render.cpp's 13 createLayout sites. Held by
// tests/qt_entry_tests.cpp, whose two assertions read count 0 before this function existed.
//
// THAT ABSENCE WAS NOT COSMETIC, and the figure is worth carrying next to the declaration
// because the obvious reasoning says otherwise. Measured on 121 printed pages across four
// fixtures on the Windows backend: installing no typography differs from the shipped
// rendering on 85 of them, and installing the three tags but dropping `kern` differs on
// 112. So the Qt stack was on the 85-pages-different side of that comparison, and the only
// instrument sensitive enough to say so runs on the backend that was already correct.
//
// WHAT THIS DOES NOT DO: make the two backends render alike. It makes them hold the same
// data. QtTextLayout::SetTypography can act on one of the three tags and that one is
// already Qt's default, so Qt renders on shaper defaults either way -- the divergence is
// now measured and named instead of invisible, and closing it needs QFont::setFeature and
// therefore a Qt floor of 6.7, which is Alex's call. See src/qt_init.cpp's call site for
// the full table, including the one prediction it refuted.
//
// Not idempotent-sensitive: the sets are values, so a second call overwrites rather than
// accumulating, which is why the Windows path's single call from WinMain and the Qt path's
// single call from installQtBackends need no release counterpart. There is nothing to free
// -- App used to hold two IDWriteTypography objects here and holds two values now.
void installTypography(App& app);

// Creates all sixteen against App's CURRENT theme, contentScale and zoomFactor, and
// re-measures the four cached space widths. Idempotent: it releases first, so the theme
// change, zoom step and DPI change that call it repeatedly cannot stack formats.
//
// Requires App::textFactory to be seated. Returns having created nothing if it is not --
// stated rather than asserted, because the Windows caller seats it in initD2D and the Qt
// caller in installQtBackends, and a caller that forgets would otherwise get sixteen
// nulls and a blank document with no complaint from anything.
void installDocumentTextFormats(App& app);

// Drops all sixteen and zeroes the four space widths. A stale width against a released
// format is the same class of bug as a stale wrapper over a released native, and it would
// survive a reinstall silently because the new formats measure the same.
void releaseDocumentTextFormats(App& app);

#endif // TINTA_TEXT_FORMATS_H

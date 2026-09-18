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
// the ~20 others are updateOverlayFormats, ensureThemePreviewFormats and
// createTypography, and those three still create theirs natively. That is why App still
// carries Seated<> storages next to the plain unique_ptrs these use -- a mixed shape that
// is temporary by construction, since the second kind disappears as each of the other
// three functions follows this one.
//
// PORTABLE: nothing here or in src/text_formats_app.cpp names a Windows type, which is
// what lets a non-Windows build create a document format at all. Before this existed, off
// Windows App had a text FACTORY and no text FORMATS -- the first document handed to a Qt
// window segfaulted on a null baseFormat -- and the fix lived in a Qt-only file, so the
// portability was real and unshared.

struct App;

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

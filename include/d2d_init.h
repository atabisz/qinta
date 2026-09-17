#ifndef TINTA_D2D_INIT_H
#define TINTA_D2D_INIT_H

#include "app.h"

bool initD2D(App& app);
void applyTheme(App& app, int themeIndex);
// Tints the native title bar/border to the current theme (Windows 11;
// Windows 10 falls back to the stock dark/light caption)
void applyWindowChrome(App& app);
void updateTextFormats(App& app);
// Width of `text` in an IDWriteTextFormat, via a throwaway IDWriteTextLayout.
//
// DECLARED HERE RATHER THAN IN utils.h, WHICH IS WHERE IT LIVED. Its four call
// sites are all in d2d_init.cpp::updateTextFormats, caching the space width for
// each native format, and they are the only four in the tree -- so this is not a
// general utility that happens to be Windows-bound, it is part of the DirectWrite
// initialiser that already owns those formats. Keeping it in utils.h was what made
// that file unbuildable off Windows for the sake of one function its own portable
// half had already replaced: the tinta::TextFormat* overload in
// src/text_utils_app.cpp is the version the other ~36 measure sites call.
//
// The two overloads' agreement is asserted one layer down -- see the note in
// src/text_utils_app.cpp, which is careful about what tests/text_factory_tests.cpp
// does and does not prove.
float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format);
void enumerateSystemFontFamilies(App& app);
void updateOverlayFormats(App& app);
void ensureThemePreviewFormats(App& app);
void createTypography(App& app);
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H

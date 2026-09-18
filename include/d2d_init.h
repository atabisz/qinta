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
// THERE IS NO LONGER A NATIVE measureText DECLARED HERE. It took an
// IDWriteTextFormat*, it was the last reason a caller might have reached for this
// header to measure something, and it is deleted -- the only measureText in the
// tree is the portable one in include/utils.h taking tinta::TextFormat*. If a new
// Windows-side measurement is ever wanted, note that this header is the wrong home
// for it twice over: the reason the native overload existed at all was that it
// cached space widths for formats which are no longer native.
void enumerateSystemFontFamilies(App& app);
void updateOverlayFormats(App& app);
void ensureThemePreviewFormats(App& app);
// NO createTypography HERE EITHER. It is installTypography in include/text_formats.h,
// and it was never a Direct2D function -- it named no COM type, no factory and no
// HRESULT. What made it Windows-only was its HOME: this header, which no portable
// target includes. Two of the three functions this header still declares create
// native formats and belong here; that one never did.
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H

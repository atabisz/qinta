#ifndef TINTA_D2D_INIT_H
#define TINTA_D2D_INIT_H

#include "app.h"
// applyTheme, updateTextFormats, applyWindowChrome and enumerateSystemFontFamilies are
// declared there now, with the fourth hook installFontFallback. Included rather than
// re-declared so the ten files that reached them through this header still do.
#include "theme_apply.h"

bool initD2D(App& app);
// Width of `text` in an IDWriteTextFormat, via a throwaway IDWriteTextLayout.
//
// THERE IS NO LONGER A NATIVE measureText DECLARED HERE. It took an
// IDWriteTextFormat*, it was the last reason a caller might have reached for this
// header to measure something, and it is deleted -- the only measureText in the
// tree is the portable one in include/utils.h taking tinta::TextFormat*. If a new
// Windows-side measurement is ever wanted, note that this header is the wrong home
// for it twice over: the reason the native overload existed at all was that it
// cached space widths for formats which are no longer native.
// NO updateOverlayFormats AND NO ensureThemePreviewFormats. Both are gone from
// src/d2d_init.cpp; their bodies are installChromeFormats and ensureThemePreviewFormats
// in include/text_formats.h, and the second keeps its exact name -- which is why
// src/overlays.cpp and src/main_d2d.cpp call it without having changed a character.
// They now resolve it through text_formats.h instead of this header.
//
// NO createTypography EITHER. It is installTypography in include/text_formats.h, and it
// was never a Direct2D function -- it named no COM type, no factory and no HRESULT. What
// made it Windows-only was its HOME: this header, which no portable target includes.
//
// THE SENTENCE THAT STOOD HERE IS NOW FALSE IN ITS ARITHMETIC AND ITS CLAIM, so it is
// corrected rather than dropped: "Two of the three functions this header still declares
// create native formats and belong here; that one never did." Those two have followed the
// third. NO FUNCTION DECLARED IN THIS HEADER CREATES A TEXT FORMAT ANY MORE -- what is
// left is COM initialisation, the device, the render target, the theme and the font
// enumeration, which is the residue anti-claim A2 of this slice's ISA predicted would stay.
//
// That list shrank again on 2026-09-24: the theme and the font enumeration went to
// include/theme_apply.h, whose Windows arms are still defined in src/d2d_init.cpp. What
// this header declares itself is COM initialisation and the render target.
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H

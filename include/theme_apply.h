#ifndef TINTA_THEME_APPLY_H
#define TINTA_THEME_APPLY_H

#include "app.h"

// Switching theme and rebuilding the text formats, for both stacks.
//
// applyTheme and updateTextFormats are PORTABLE and live in src/theme_apply_app.cpp. They
// used to live in src/d2d_init.cpp, and what kept them Windows-only was that home rather than
// their bodies: applyTheme named no Windows type at all, and updateTextFormats was a portable
// skeleton wrapped round one DirectWrite block, the font fallback chain. That is the
// createTypography shape again, which include/d2d_init.h already records once.
//
// The price of leaving them there was measured on 2026-09-24 with a link fixpoint over the
// manifest: input.cpp needs all three of this header's backend hooks plus the two portable
// functions, and input.cpp is the gate for tabs.cpp, editor.cpp and the rest. README has the
// four-arm table.
void applyTheme(App& app, int themeIndex);
void updateTextFormats(App& app);

// THE THREE BACKEND HOOKS. One definition per stack: the Windows arms are in src/d2d_init.cpp,
// beside the DirectWrite factory and the HWND they need, and the Qt arms are in
// src/theme_qt.cpp. The two files are never linked into the same binary.

// Tints the native title bar/border to the current theme (Windows 11; Windows 10 falls back
// to the stock dark/light caption). Qt: does nothing, and that is a real gap -- the title bar
// keeps the platform's colours.
void applyWindowChrome(App& app);

// Builds the emoji/CJK font fallback chain once and hands it to the text factory. Called by
// updateTextFormats on every format rebuild; the build is guarded, the handoff is not.
// Qt: does nothing, because QFont resolves fallback per font from the system's own
// configuration. The Windows chain's explicit CJK ordering by UI language therefore has no
// Qt counterpart, and that is a divergence stated rather than solved.
void installFontFallback(App& app);

// Fills app.systemFontFamilies, sorted, on first call; later calls do nothing.
void enumerateSystemFontFamilies(App& app);

#endif // TINTA_THEME_APPLY_H

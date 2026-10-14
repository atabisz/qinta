#ifndef TINTA_THEME_APPLY_H
#define TINTA_THEME_APPLY_H

#include "app.h"
#include "window_sink.h"  // tinta::WindowChrome

#include <string>
#include <vector>

// Switching theme and rebuilding the text formats, for both stacks until 0b39ae6 and for the
// Qt stack, the only one, since.
//
// applyTheme and updateTextFormats are PORTABLE and live in src/theme_apply_app.cpp. They
// used to live in src/d2d_init.cpp, and what kept them Windows-only was that home rather than
// their bodies: applyTheme named no Windows type at all, and updateTextFormats was a portable
// skeleton wrapped round one DirectWrite block, the font fallback chain. That is the
// createTypography shape again, which the retired include/d2d_init.h recorded once.
//
// The price of leaving them there was measured on 2026-09-24 with a link fixpoint over the
// manifest: input.cpp needs all three of this header's backend hooks plus the two portable
// functions, and input.cpp is the gate for tabs.cpp, editor.cpp and the rest. README has the
// four-arm table.
void applyTheme(App& app, int themeIndex);
void updateTextFormats(App& app);

// FOLLOW-SYSTEM MODE, with the system's light/dark reading passed IN. Each stack read it its
// own way -- the retired WinMain the AppsUseLightTheme registry value, the Qt arm QStyleHints
// (qt_launch.h) -- and what to do with the reading is this one body, so the two could not
// disagree about it.
//
// The theme follow mode wants for that reading: the saved light or dark preference.
int followThemeIndex(const App& app, bool systemPrefersLight);
// WM_SETTINGCHANGE "ImmersiveColorSet": with follow on and the wanted theme not already
// showing, switch to it and persist the choice. Returns whether it switched.
bool followSystemAppearance(App& app, bool systemPrefersLight);

// The title bar for the current theme: the dark caption variant, the caption and border fill
// -- the drawn tab strip's colour, so the frame blends with it -- and the caption text. One
// body, read by src/theme_qt.cpp's applyWindowChrome (and, until 0b39ae6, by the Direct2D
// stack's arm too).
tinta::WindowChrome windowChromeFor(const App& app);

// THE THREE BACKEND HOOKS. One definition each, in src/theme_qt.cpp: applyWindowChrome,
// installFontFallback and enumerateSystemFontFamilies. The Windows arms were in the retired
// src/d2d_init.cpp, beside the DirectWrite factory and the HWND they needed.

// Tints the native title bar/border to windowChromeFor(app) (Windows 11; Windows 10 falls back
// to the stock dark/light caption). Qt: delivered through the window sink, and the window
// paints it with the same DWM calls on Windows. On Linux and macOS it is still a real gap --
// the title bar keeps the platform's colours. See src/theme_qt.cpp.
void applyWindowChrome(App& app);

// Hands the emoji/CJK fallback families to the text factory, which holds them and applies
// them to no layout: a layout gets them only where a caller asks, through
// tinta::TextLayout::ApplyFontFallback, and there are four such callers. Called by
// updateTextFormats on every format rebuild AND by installQtBackends at startup -- the
// second because updateTextFormats is not on the Qt startup path at all (src/qt_init.cpp
// says why), so the installer alone would never run at a default launch.
//
// The CJK ordering by UI language (#155) is live on every platform since 2026-10-12, where
// this used to say it "has no Qt counterpart". It does: a QFont walks its families list in
// order. What has no Qt counterpart is DirectWrite's per-RANGE mapping -- see
// src/theme_qt.cpp for what that costs.
void installFontFallback(App& app);

// The ordered family list #155 is about, as a PURE function of a BCP 47 UI language tag:
// "ja"/"ja-JP" lead with the Japanese faces, "ko"/"ko-KR" with the Korean ones, and
// everything else -- including an unknown or empty tag -- takes the Simplified-Chinese-first
// default the deleted Direct2D arm used. Each language's group carries the Windows, macOS and
// Linux faces, and the emoji faces come last.
//
// Separated from installFontFallback, and declared here rather than left file-static, so the
// ordering is testable WITHOUT depending on which families the host happens to have
// installed -- which families resolve is Qt's business at shaping time, and asserting on it
// would make this untestable in CI. Defined in src/theme_qt.cpp.
std::vector<std::wstring> cjkFallbackFamilies(const std::wstring& uiLanguageTag);

// The families of `families` the host has installed, in their original order. What
// installFontFallback seats, with QFontDatabase::hasFamily as `installed`.
//
// NOT AN OPTIMISATION: an absent family is not free on macOS. NanumGothic is in Apple's
// downloadable-font catalogue, so when Qt walks the list to it for a glyph nothing earlier
// covers, QCoreTextFontDatabase::fallbacksForFamily asks CoreText to match it and CoreText
// downloads the asset SYNCHRONOUSLY, blocking the layout until it lands. Measured 2026-10-13:
// blockquote_layout and math_layout sat at 0% CPU in TDownloadableFontManager::Download from
// 09:28, and the asset (com_apple_MobileAsset_Font8, NanumGothic.ttc) arrived at 10:12 -- 44
// minutes. The CI job was killed at 55 without it. With this filter the suite runs in 12 s.
// hasFamily enumerates installed faces, not downloadable ones, so it reads Nanum as absent --
// and still does after the download. Pure, with the predicate passed in, so the filter is
// testable on a host that has none of the families.
std::vector<std::wstring> installedFamiliesOnly(
    const std::vector<std::wstring>& families,
    bool (*installed)(const std::wstring& family));

// Fills app.systemFontFamilies, sorted, on first call; later calls do nothing.
void enumerateSystemFontFamilies(App& app);

#endif // TINTA_THEME_APPLY_H

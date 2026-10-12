#ifndef TINTA_TABS_H
#define TINTA_TABS_H

#include "app.h"
// Real windows.h here, stand-ins for its value types off Windows. This header
// names HWND/WPARAM/LPARAM by value and nothing else from Win32 -- no COM
// interfaces, no API calls -- so that swap is the whole of its port cost.
#include "win32_types.h"

// Tabbed interface: Win11 Notepad-style tabs living in the custom title
// bar. Single-file windows stay tabless (the strip shows the title text);
// the tab row appears when a second document opens.

// Model
void tabsInit(App& app);                       // adopt the startup document
// Rebuild the row from last session's paths (startup doc becomes active)
void tabsSeedSession(App& app, const std::vector<std::string>& paths);
void tabActivate(App& app, HWND hwnd, int index);
void tabOpenPath(App& app, HWND hwnd, const std::string& utf8Path,
                 bool activate = true);
// The start page giving way to a document: the document opens as a tab and
// the start page's own tab closes, so a launcher holding nothing becomes a
// window holding that one document. The start page's recent-file click, and
// a macOS Finder open that arrives while the start page is showing.
void tabOpenPathReplacingStartPage(App& app, HWND hwnd,
                                   const std::string& utf8Path);
void tabCloseIndex(App& app, HWND hwnd, int index);
void tabCycle(App& app, HWND hwnd, int direction);

// Title-bar geometry shared by rendering and non-client hit testing
tinta::RectF captionButtonRect(const App& app, int button);  // 0 min 1 max 2 close
float captionIslandLeft(const App& app);
int captionHitTest(const App& app, float x, float y);       // 0 none, 1..3
// Strip surface color derived from the theme (shared with the DWM frame
// tint so border and transition flashes match the drawn caption)
tinta::Color tabStripBackground(const App& app);
// + button: a new untitled quick-note tab, straight into the editor
void tabOpenQuickNote(App& app, HWND hwnd);
void tabOpenStartPage(App& app, HWND hwnd);
void tabBecomeStartPage(App& app, HWND hwnd);

// Rendering (main_d2d render loop)
void renderTabStrip(App& app);
void renderTabSwitcher(App& app);

// Tab row vs tabless title text (drag-out satellites force the row)
bool tabStripVisible(const App& app);

// Right-click tab context menu (NPP-style close operations)
int tabContextMenuIndexAt(const App& app, float x, float y);
void openTabMenu(App& app, int tabIndex, float x, float y);
void closeTabMenu(App& app);
int tabMenuItemAt(const App& app, float x, float y);
void renderTabMenu(App& app);
bool tabMenuMouseDown(App& app, HWND hwnd, int x, int y);
// Continue a Close-others/left/right sweep after a dirty tab's dialog
void tabBulkCloseStep(App& app, HWND hwnd);

// Mouse handling for the strip area; return true when consumed
bool tabStripMouseDown(App& app, HWND hwnd, int x, int y, bool middle);
bool tabSwitcherMouseDown(App& app, HWND hwnd, int x, int y);
// Drag lifecycle: move reorders inside the strip or floats the tab as a
// ghost card once clear of it; release decides (spawn a new window or snap
// back -- NOT a drop into another running window, which this stack cannot
// ask for; see docs/roadmap.md); Esc/capture loss cancels
void tabDragMove(App& app, HWND hwnd, int x, int y);
void tabDragEnd(App& app, HWND hwnd, int x, int y);
void tabDragCancel(App& app, HWND hwnd);
// DEFERRED, AND UNCALLED ON THIS STACK. On Win32 this ran after a native
// window move: a tabless window dropped onto another Tinta window's strip
// donated its document there. Its one caller died with the Direct2D window
// procedure, and the search it ran was an HWND walk, so nothing calls it
// here and the behaviour does not happen. docs/roadmap.md groups it with
// the other direction of the same missing question.
void tabWindowDropMerge(App& app, HWND hwnd);

#endif  // TINTA_TABS_H

#ifndef TINTA_INPUT_H
#define TINTA_INPUT_H

#include <cstdint>
#include <vector>

#include "app.h"
#include "post_sink.h"
// Real windows.h here, stand-ins for its value types off Windows. This header
// names HWND by value and nothing else from Win32 -- no COM interfaces, no API
// calls, and since the entry points below stopped taking message parameters,
// no WPARAM or LPARAM either -- so that swap is the whole of its port cost.
#include "win32_types.h"

// These nine took WPARAM/LPARAM until 2026-09-20 and now take the data they
// actually use. The decoding did not disappear; it moved UP, into the window
// procedure of the retired src/main_d2d.cpp (deleted in 0b39ae6), and the
// callers now are the Qt event handlers in src/qt_window.cpp, which call all
// nine with no Win32 anywhere in the path.
//
// Why it was worth doing: the re-extraction was 59 GET_*_LPARAM uses across
// four of these handlers -- 27 GET_X_LPARAM, 27 GET_Y_LPARAM, 5
// GET_WHEEL_DELTA_WPARAM, on 55 lines, four of which carry two. The figure read 55
// here and in src/input.cpp until 2026-09-20, because 55 is also what g++ reports:
// it names an undeclared identifier once per FUNCTION, not once per use. Three
// different quantities, two of them 55. Anything asserting a count of uses says 59.
// That include is what made <windowsx.h> a FATAL include for
// the Linux portability census, and the census cannot see a dependency carried
// by a parameter TYPE at all -- so the file read "errors 0" while being the
// least portable one in the tree. Coordinates are CLIENT-space pixels.

// wheelDelta is in notches, already divided by WHEEL_DELTA: +1.0 is one click
// away from the user. Fractional values are what high-resolution wheels send.
void handleMouseWheel(App& app, float wheelDelta);
void handleMouseHWheel(App& app, float wheelDelta);
void handleMouseMove(App& app, int mouseX, int mouseY);
// Neither of these two reads a button or modifier state off the message: they
// ask GetKeyState and app state directly, so the WPARAM they used to take was
// already unused before it was removed.
void handleMouseDown(App& app, int mouseX, int mouseY);
// WM_MBUTTONDOWN's case: closes the tab under the press, and ONLY inside the tab strip.
// Returns whether it consumed the press, which is that case's `return 0` against its
// `break` -- a middle press anywhere else is the widget's to ignore. Separate from
// handleMouseDown because it was a separate case: that function takes no button, and the
// middle button acts at exactly one place.
bool handleMiddleMouseDown(App& app, int mouseX, int mouseY);
void handleMouseUp(App& app, int mouseX, int mouseY);
// True consumes this keystroke's character translation as well (#195).
bool handleKeyDown(App& app, unsigned key);
// fromKeyboard is the menu key rather than a right-click, which Win32 signals
// as a (-1, -1) position; the caller tests for that and does the screen-to-
// client conversion, so mouseX/mouseY here are ALWAYS client-space and are
// ignored entirely when fromKeyboard is true (the menu opens centred).
void handleContextMenu(App& app, int mouseX, int mouseY,
                       bool fromKeyboard);
// One of the drawn menus' CTX_* commands, run as a click on its item runs it,
// then a repaint. For a caller outside those menus: the macOS menu bar's items
// that have no key to replay (include/menu_bar_qt.h).
void invokeMenuCommand(App& app, int command);
// Zen mode on or off, as the zen key does. For the macOS menu bar's View item, whose
// ⌃⌘F is not a key the router knows (include/menu_bar_qt.h).
void toggleZenMode(App& app);
// NOT ONLY TYPED TEXT. `character` also carries SHORTCUT ROUTING: under the vim
// key profile the punctuation bindings are matched on the character rather than
// the virtual key, so '?' reaching here is what opens Help, and a full-width
// colon is what enters the editor. Measured by a plant that corrupted this one
// parameter -- it broke two keymap assertions alongside the five text ones, which
// is the difference between "this argument is the text" and "this argument is the
// keystroke's layout-resolved meaning". A caller that filters it to printables
// would silently disable those bindings.
void handleCharInput(App& app, wchar_t character);
// Unfiltered: every dropped path arrives and this decides which ones it
// supports. The caller owns the HDROP and its DragFinish.
void handleDropFiles(App& app,
                     const std::vector<std::wstring>& paths);
// Load a document into the viewer, saving the outgoing reading position
// and restoring the incoming one (shared with tabs.cpp)
bool openDocumentInViewer(App& app, const std::wstring& fullPath);
// Mouse XBUTTON / Alt+arrow navigation across link jumps (viewer)
void navigateBack(App& app);
void navigateForward(App& app);
void handleFileWatchTimer(App& app);
void handleSelectScrollTimer(App& app);
// Dwell timer fired over a local .md link: load the peek preview
void handleLinkPeekTimer(App& app);
// One tick of timer `id`: WM_TIMER's dispatch, called from the Qt window's timer sink (and
// shared with the retired WinMain until 0b39ae6).
void handleTimer(App& app, unsigned id);
// WM_CLOSE's question, asked by the Qt window's closeEvent (and the retired WinMain's case): true when
// the window may close now. False when an unsaved buffer has to be resolved first, in
// which case it has already activated that tab and opened the unsaved-changes dialog
// (or, with the dialog open, marked the close as pending behind it).
bool windowCloseMayProceed(App& app);
// A posted message (tinta::PostedMessage, include/post_sink.h), on the window's thread, taking
// ownership of what `pointer` points at. The enum is the list of messages: this switches over
// every one with no default, as the Qt window's post sink does, so a message added without a
// case fails the Linux build. Close and SettingChange are answered by the window itself and
// reach here only as a no-op. This replaced handlesPostedMessage's allowlist, which twice fell
// behind the handler (WM_APP_LAYOUT_CHUNK, then WM_APP_PANDOC_DONE) and refused the message.
void handlePostedMessage(App& app, tinta::PostedMessage message, std::uintptr_t value,
                         std::intptr_t pointer);

#endif // TINTA_INPUT_H

#ifndef TINTA_INPUT_H
#define TINTA_INPUT_H

#include <vector>

#include "app.h"
// Real windows.h here, stand-ins for its value types off Windows. This header
// names HWND by value and nothing else from Win32 -- no COM interfaces, no API
// calls, and since the entry points below stopped taking message parameters,
// no WPARAM or LPARAM either -- so that swap is the whole of its port cost.
#include "win32_types.h"

// These nine took WPARAM/LPARAM until 2026-09-20 and now take the data they
// actually use. The decoding did not disappear; it moved UP, into
// src/main_d2d.cpp's window procedure, which is the only thing here that has
// a message to decode and is the file the Qt cutover deletes. A Qt event
// handler can therefore call all nine with no Win32 anywhere in the path.
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
void handleMouseWheel(App& app, HWND hwnd, float wheelDelta);
void handleMouseHWheel(App& app, HWND hwnd, float wheelDelta);
void handleMouseMove(App& app, HWND hwnd, int mouseX, int mouseY);
// Neither of these two reads a button or modifier state off the message: they
// ask GetKeyState and app state directly, so the WPARAM they used to take was
// already unused before it was removed.
void handleMouseDown(App& app, HWND hwnd, int mouseX, int mouseY);
void handleMouseUp(App& app, HWND hwnd, int mouseX, int mouseY);
// True consumes this keystroke's character translation as well (#195).
bool handleKeyDown(App& app, HWND hwnd, unsigned key);
// fromKeyboard is the menu key rather than a right-click, which Win32 signals
// as a (-1, -1) position; the caller tests for that and does the screen-to-
// client conversion, so mouseX/mouseY here are ALWAYS client-space and are
// ignored entirely when fromKeyboard is true (the menu opens centred).
void handleContextMenu(App& app, HWND hwnd, int mouseX, int mouseY,
                       bool fromKeyboard);
// NOT ONLY TYPED TEXT. `character` also carries SHORTCUT ROUTING: under the vim
// key profile the punctuation bindings are matched on the character rather than
// the virtual key, so '?' reaching here is what opens Help, and a full-width
// colon is what enters the editor. Measured by a plant that corrupted this one
// parameter -- it broke two keymap assertions alongside the five text ones, which
// is the difference between "this argument is the text" and "this argument is the
// keystroke's layout-resolved meaning". A caller that filters it to printables
// would silently disable those bindings.
void handleCharInput(App& app, HWND hwnd, wchar_t character);
// Unfiltered: every dropped path arrives and this decides which ones it
// supports. The caller owns the HDROP and its DragFinish.
void handleDropFiles(App& app, HWND hwnd,
                     const std::vector<std::wstring>& paths);
// Load a document into the viewer, saving the outgoing reading position
// and restoring the incoming one (shared with tabs.cpp)
bool openDocumentInViewer(App& app, const std::wstring& fullPath);
// Mouse XBUTTON / Alt+arrow navigation across link jumps (viewer)
void navigateBack(App& app, HWND hwnd);
void navigateForward(App& app, HWND hwnd);
void handleFileWatchTimer(App& app, HWND hwnd);
void handleSelectScrollTimer(App& app, HWND hwnd);
// Dwell timer fired over a local .md link: load the peek preview
void handleLinkPeekTimer(App& app, HWND hwnd);
// One tick of timer `id`: WM_TIMER's dispatch, shared by WinMain and the Qt window's
// timer sink.
void handleTimer(App& app, HWND hwnd, unsigned id);
// WM_CLOSE's question, shared by WinMain's case and the Qt window's closeEvent: true when
// the window may close now. False when an unsaved buffer has to be resolved first, in
// which case it has already activated that tab and opened the unsaved-changes dialog
// (or, with the dialog open, marked the close as pending behind it).
bool windowCloseMayProceed(App& app, HWND hwnd);
// The posted messages the Qt window's post sink (include/post_sink.h) takes. SIX, and this
// comment is the authority on none of them -- handlesPostedMessage's body is, and this list
// has been behind it twice: it named four until 2026-10-12, missing WM_APP_LAYOUT_CHUNK
// (added 2026-10-06) and then WM_APP_PANDOC_DONE (added the same day this was corrected).
// Read the function, and if you are adding a message, this sentence is the one that rots.
//
//   WM_APP_IMAGE_READY, WM_APP_FOLDER_SEARCH   posted by portable worker threads
//   WM_APP_LAYOUT_CHUNK, WM_APP_PANDOC_DONE    posted from the window's own thread and the
//                                              pandoc worker respectively, handled below
//   WM_CLOSE, WM_SETTINGCHANGE                 the window answers these itself rather than
//                                              through here
//
// handlesPostedMessage runs on the POSTING thread, so it reads nothing but its argument;
// handlePostedMessage runs on the window's thread and takes ownership of lParam, as the
// window procedure's two cases do. WinMain keeps its own cases and does not call these.
bool handlesPostedMessage(unsigned message);
void handlePostedMessage(App& app, unsigned message, WPARAM wParam, LPARAM lParam);

#endif // TINTA_INPUT_H

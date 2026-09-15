#ifndef TINTA_EDITOR_H
#define TINTA_EDITOR_H

#include "app.h"
// For toUtf8, which used to be declared in this header. Included explicitly and
// not left to arrive through app.h: a transitively-available declaration is a
// dependency nothing states and any include reshuffle can remove silently.
#include "text_utils.h"
// Real windows.h here, stand-ins for its value types off Windows. This header
// names HWND/WPARAM/LPARAM by value and nothing else from Win32 -- no COM
// interfaces, no API calls -- so that swap is the whole of its port cost.
#include "win32_types.h"

// Mode transitions
void enterEditMode(App& app);
// Ctrl+N quick note: edit mode on an untitled empty buffer with no backing
// file; the first save routes through the classic Save As dialog
void enterQuickNoteMode(App& app);
void enterRecoveredDraft(App& app, HWND hwnd, const std::string& content,
                         const std::string& origPath);
void exitEditMode(App& app);

// Editor input handlers
void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam);
void editorPositionImeWindow(App& app, HWND hwnd);
void handleEditorMouseDown(App& app, HWND hwnd, int x, int y);
void handleEditorMouseUp(App& app, HWND hwnd, int x, int y);
void handleEditorMouseMove(App& app, HWND hwnd, int x, int y);
void handleEditorMouseWheel(App& app, HWND hwnd, float delta);

// Editor rendering
void renderEditor(App& app, float editorWidth);

// Re-enter edit mode from a parked tab buffer (tabs.cpp), restoring the
// dirty flag, scroll, and caret the switch-away captured
void restoreEditBuffer(App& app, const std::wstring& text, bool dirty,
                       float scrollY, size_t cursor);

// Quick-note empty state (design 4a): an untitled buffer with no text yet
// shows a centered "Open a file" button in the preview pane until typing
// starts; the button (or Ctrl+O) opens the classic file picker
bool quickNoteEmptyStateActive(const App& app);
// Open in a tab, preserving existing/unsaved documents in either mode.
void openFileDialog(App& app, HWND hwnd);
void renderQuickNoteEmptyState(App& app);

// File save; Save As (Ctrl+Shift+S) re-prompts for the path and moves the
// tab's identity to the chosen file
void saveEditorFile(App& app, HWND hwnd);
void saveEditorFileAs(App& app, HWND hwnd);
// Ctrl+Shift+S anywhere: editor writes the buffer to a new path, viewer
// copies the viewed file; the tab follows the new name either way
void saveFileAs(App& app, HWND hwnd);
// Unsaved-changes dialog outcome: 1 save+exit, 2 discard, 3 keep editing
void confirmExitAction(App& app, HWND hwnd, int action);

// Editor reparse (called from timer)
// Exports need the current buffer even when the preview pane is hidden.
void editorReparse(App& app, bool force = false);
// External edits: undo-capable range replace plus dirty + immediate
// reparse, used by the preview table cell editor (#148)
void editorReplaceRangeExternal(App& app, size_t start, size_t end,
                                const std::wstring& repl);
void editorMarkDirtyAndReparse(App& app);

// Editor search
void performEditorSearch(App& app);
void scrollEditorToMatch(App& app);
// Find & replace (#121): replace the current match / every match with
// App::replaceText; each hunk is one undo entry
void editorReplaceCurrent(App& app, HWND hwnd);
void editorReplaceAll(App& app, HWND hwnd);

// Utility
void rebuildLineStarts(App& app);
size_t editorTopVisibleLine(App& app);
// toUtf8 moved to text_utils.h, included above, so every file that reached it
// through editor.h still does. It is declared there beside toWide because that is
// where its body now lives -- see the note there.

#endif // TINTA_EDITOR_H

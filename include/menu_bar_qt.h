#ifndef TINTA_MENU_BAR_QT_H
#define TINTA_MENU_BAR_QT_H

class QKeyCombination;
class QKeyEvent;
class QMenuBar;
class QWidget;
struct App;

// THE macOS MENU BAR (2026-10-13). There was none -- 0 hits for QMenuBar -- so on macOS the
// screen's top bar held only what Qt's Cocoa backend builds by itself: the application menu with
// Hide and Quit. The drawn application menu (the window's top-left icon) stays as it is; this is
// the platform's own menu offering the same commands where a Mac user looks for them.
//
// AN ITEM WHOSE KEY THE ROUTER OWNS REPLAYS THAT KEY, rather than calling the command behind it,
// and that is the design rather than a shortcut. The router's answer depends on the mode: Ctrl+A
// selects the editor's buffer while editing and the document while reading, and CTX_SELECT_ALL
// only knows the second. Replaying the keystroke into the window's keyPressEvent makes a click do
// exactly what the key does in every mode, and makes the key binding the one authority. Items
// with no key (Export, Help) call the drawn menu's action (invokeMenuCommand, include/input.h).
//
// Quit, Hide and Minimize are left to Qt and Cocoa. Measured before this menu existed: Cmd+Q
// quits and Cmd+H hides (the Cocoa item wins over the router's Ctrl+H find-and-replace), and
// Cmd+M does nothing. keyStaysWithWindow keeps those three out of the window's hands so a menu
// bar does not change them.
namespace tinta {

// Fills `bar` with the application menu's Settings, File, Edit, View and Help, labelled in the UI
// language. On macOS a QMenuBar with no parent is the global menu bar. The labels follow a later
// language change: the bar relabels itself on the window's next paint after the index moves.
void populateMenuBar(QMenuBar& bar, App& app, QWidget& window);

// Redoes every title and item text from its translation key in the current UI language.
void relabelMenuBar(QMenuBar& bar, const App& app);

// The ShortcutOverride answer: true when `event` is a key the window routes, so Qt delivers it to
// keyPressEvent instead of firing a menu shortcut. Every key the router could act on stays with
// it, as before the menu existed, except Cmd+Q, Cmd+H and Cmd+M, which stay the platform's, and
// the menu's two Mac keys, zen and find-and-replace, which stay the menu's.
bool keyStaysWithWindow(const QKeyEvent& event);

// The View menu's Zen mode key: Meta+Ctrl+F, which macOS shows as ⌃⌘F.
QKeyCombination zenMenuKey();

// Edit's Find and Replace key: Alt+Ctrl+F, which macOS shows as ⌥⌘F. The router's own key is
// Ctrl+H, which macOS takes for Hide.
QKeyCombination findReplaceMenuKey();

}  // namespace tinta

#endif  // TINTA_MENU_BAR_QT_H

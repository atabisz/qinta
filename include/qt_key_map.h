#ifndef TINTA_QT_KEY_MAP_H
#define TINTA_QT_KEY_MAP_H

// QT KEY CODE -> WINDOWS VIRTUAL-KEY CODE, SO src/input.cpp's handleKeyDown CAN BE THE
// QT STACK'S KEY HANDLER TOO.
//
// handleKeyDown matches on virtual-key codes -- `case VK_F11`, `case 'Q'`, `vk >= VK_F1 &&
// vk <= VK_F12` -- and key profiles in src/keymap.cpp store bindings the same way. Rather
// than a second set of bindings keyed on Qt::Key, the Qt window translates the key once at
// the edge and hands the handler the code it already understands. The same move the mouse
// entry points made when they stopped taking LPARAM: the decoding moves UP, into the
// event handler that has the event.
//
// COVERAGE IS THE CODES THE HANDLERS NAME, NOT THE KEYBOARD. Every VK constant
// include/keymap.h defines off Windows -- which is by that header's own admission rule
// exactly the set a portable file names -- plus A-Z and 0-9, which handleKeyDown matches as
// character literals. Anything else returns 0, and 0 means "no virtual key": the caller
// then offers the key's TEXT to handleCharInput, which is where punctuation bindings are
// matched anyway (include/input.h, "NOT ONLY TYPED TEXT").
//
// SOME CODES ARE THE SAME NUMBER AND SOME ARE NOT, which is why this is a table and not a
// cast. Qt::Key_A is 0x41 and so is 'A'; Qt::Key_Space is 0x20 and so is VK_SPACE. Qt::Key_F11
// is 0x0100003A and VK_F11 is 0x7A. A cast would get the letters right, get every function
// key, arrow and Escape wrong, and pass any test that only typed letters.
//
// SHIFTED PUNCTUATION IS DELIBERATELY NOT MAPPED. Qt reports Shift+/ on a US layout as
// Qt::Key_Question, where Windows reports VK_OEM_2 with Shift held. Mapping Key_Question back
// to VK_OEM_2 would be right on one layout and wrong on the rest; the '?' binding is matched
// on the character, so it arrives through handleCharInput without this table.
//
// The parameter is `int` rather than Qt::Key so this header stays Qt-free: a QKeyEvent's
// key() is an int already, and nothing that includes this needs Qt's headers to call it.

namespace tinta {

// The virtual-key code for a Qt key code, or 0 when the handlers name no code for it.
unsigned vkFromQtKey(int qtKey);

}  // namespace tinta

#endif  // TINTA_QT_KEY_MAP_H

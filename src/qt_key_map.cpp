#include "qt_key_map.h"

#include <Qt>

#include "keymap.h"  // the VK_* codes, real <windows.h> on Windows and literals elsewhere

namespace tinta {

unsigned vkFromQtKey(int qtKey) {
    // Letters and digits are the one range where the two numbering schemes coincide, so
    // they pass through. The check is on the Qt side's own constants, so a Qt that ever
    // renumbered them would fail here rather than silently shift every letter binding.
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return static_cast<unsigned>('A' + (qtKey - Qt::Key_A));
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return static_cast<unsigned>('0' + (qtKey - Qt::Key_0));
    }
    // F1..F12 by offset, relying on the contiguity keymap.h static_asserts for VK_F1..VK_F12
    // and Qt guarantees for Key_F1..Key_F35. F13 and up are not named by any handler.
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        return static_cast<unsigned>(VK_F1 + (qtKey - Qt::Key_F1));
    }

    switch (qtKey) {
        case Qt::Key_Backspace: return VK_BACK;
        // Backtab is what Qt reports for Shift+Tab; Windows reports VK_TAB with Shift held,
        // and handleKeyDown reads the Shift state itself.
        case Qt::Key_Tab:
        case Qt::Key_Backtab: return VK_TAB;
        // Enter is the keypad's key. Windows gives both VK_RETURN.
        case Qt::Key_Return:
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Shift: return VK_SHIFT;
        case Qt::Key_Control: return VK_CONTROL;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_End: return VK_END;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Comma: return VK_OEM_COMMA;
        case Qt::Key_Slash: return VK_OEM_2;
        default: return 0;
    }
}

}  // namespace tinta

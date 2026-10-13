# Tab ghost: the *same* font as the tab

Open this file and its [companion](copy-file-path-companion.md) as tabs, then drag
this tab out of the strip and hold it there. The floating card (the "ghost") shows
the title in **the same family as the tab strip**:

| Platform | Tab strip and ghost |
| --- | --- |
| Windows | Segoe UI |
| macOS | Helvetica Neue |
| Linux | DejaVu Sans |

- Compare the letter shapes of `tab-ghost-font.md` in the card with the tabs still in
  the strip. Before 2026-10-13 the card asked for "Segoe UI" on every OS, and on macOS
  Qt substituted the system UI font (`.AppleSystemUIFont`).
- A long title is elided with "…", not clipped.
- Press **Esc** while dragging: the card disappears and the mouse is released.

> A quote, `code` and $a^2+b^2=c^2$ in the same document.

# macOS menu bar: $E = mc^2$

Regression fixture for the macOS menu bar. Open this file in Qinta on a Mac and use
the menu bar at the top of the screen. Every item with a key must do what the key does.

## Existing formatting

| Menu | Item | Expected |
| --- | --- | --- |
| Qinta | Settings… (⌘,) | The Settings panel opens |
| File | Open File… (⌘O) | The open dialog appears |
| File | Print / PDF (⌘P) | Print preview opens; $\frac{1}{2}$ prints |
| Edit | Select All (⌘A) | This whole document is selected |
| Edit | Search (⌘F) | The search bar opens |
| View | Zen mode (⌃⌘F) | Zen mode: full screen, centred column; ⌃⌘F again leaves |
| Help | Help | The shortcut sheet opens |

> A quote with `code` and $a+b=c$ should keep its bar height while you select it.

- Press **E** to edit, then choose **Edit > Select All**. The *editor's* text must be
  selected, exactly as ⌘A selects it, not the preview beside it.
- With the editor open, press ⌘A on the keyboard: still the editor's text.
- ⌘H still hides Qinta, ⌘Q still quits, and ⌘M still does nothing.
- Press ⌃⌘F on the keyboard: zen mode, not the search bar. ⌘F alone still searches,
  and Esc leaves zen mode as before.
- [A link](https://example.com) and **bold** text are part of the selection.

```text
A code block with one row.
```

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
$$

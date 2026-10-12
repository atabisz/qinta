# Finder open: $E = mc^2$

Regression fixture for macOS **Open With** and double-click. Quit Qinta, then
double-click this file in Finder (or choose **Open With > Qinta**). It must open
in the window that starts, as the only tab: no start page left beside it.

## Existing formatting

| Feature | Example |
| --- | --- |
| Inline code | `QFileOpenEvent` |
| Math | $\frac{1}{2}$ |
| Emphasis | **bold**, *italic* and a [link](https://example.com) |

> A quote with `code` and $a+b=c$ should keep its bar height.

- Cold: with Qinta not running, double-click this file. One window, this document.
- Warm: with this open, double-click the [companion](finder-open-companion.md) in
  Finder. It joins this window as a second tab; no second Qinta appears in the Dock.
- Drop this file on the Dock icon: the same as a double-click.

```text
A code block with one row.
```

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
$$

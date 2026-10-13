# Reveal in file manager: *selected*, not just opened

Right-click this document, or its tab, and choose the reveal item. Its label names the
platform's file manager: **Reveal in Explorer** on Windows, **Reveal in Finder** on
macOS, **Show in File Manager** on Linux. In every case the folder holding this file
opens **with this file selected**.

Save this file into a folder whose name has a space and a non-ASCII character
(`reveal tést/`) before checking, so the path travels as one argument.

## What to check

| Platform | Label | Expected |
| --- | --- | --- |
| Windows | Reveal in Explorer | Explorer opens, file selected |
| macOS | Reveal in Finder | Finder comes forward, file selected |
| Linux (GNOME, KDE, Cinnamon, MATE, Xfce) | Show in File Manager | File manager opens, file selected |
| Linux, no FileManager1 service | Show in File Manager | The folder opens, nothing selected |

1. Switch the UI language to Deutsch, Français, 日本語 and 한국어 in Settings. The
   label names the same file manager in each, and the **Open in tabs** hint under
   Settings names it too ("Files opened from the Finder…" on macOS).
2. Right-click an **inactive** tab and reveal it: the selected file is that tab's, not
   the active one's. Use the [companion document](copy-file-path-companion.md).
3. An untitled note has no path, so its reveal item is disabled.

> A quote with `inline code`, a [link](https://example.com) and $e^{i\pi}+1=0$ stays
> laid out as before.

```sh
open -R "reveal tést/reveal-in-file-manager.md"   # what macOS runs
```

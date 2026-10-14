# A second launch into a window showing the start page

With "Open files in tabs" on, opening a file while qinta is already running hands the file to the
running window. If that window is showing the start page, the document now takes the start page's
place, as a click on a recent file does. It used to open beside it, leaving an empty start-page tab.

**By hand, on Windows:**

1. Start qinta with no file, so it shows the start page.
2. Double-click this file in Explorer, or run `qinta second-launch-start-page.md`.
3. The window comes forward with **one** tab, this document. Before the fix there were two: the
   start page and this document.
4. Press Ctrl+T for a new start-page tab, then open another file the same way. The start-page tab
   is replaced again, and the other tabs are untouched.

| Window shows | A handed-over document |
| --- | --- |
| the start page | replaces it |
| a document | opens as a new tab beside it |

> The same rule applies to a macOS Finder open (`tests/fixtures/finder-open.md`).

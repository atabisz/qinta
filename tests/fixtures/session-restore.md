# Session and settings restore

Fixture for qinta's restore of the saved settings and the last session (2026-10-11). Open this
file with two other documents as **tabs**, change the settings below, close the window normally,
then start qinta with **no file argument**.

## What should come back

| Setting | How to change it | After a plain relaunch |
|---|---|---|
| Open tabs | open three documents with *Open files in tabs* on | the same three tabs, the same one active |
| Reading position | scroll this file halfway down | it reopens at the same place |
| Zoom | `Ctrl` + wheel | the same zoom |
| Reading width | the settings slider | the same width |
| Word wrap, preview | the editor toggles | as left |
| UI language | the language picker | the same language, not the system's |

> A quick note started with `qinta --new`, or a window started with `--cascade`, does not own the
> session: closing one leaves the main window's tabs as they were.

1. Tabs that point at files deleted in the meantime are dropped.
2. The active tab follows its own path through that filtering.
3. A launch **with** a file argument opens that file and leaves the session alone.

- [ ] Run `ctest -R settings_restore` for the automated half: the round trip of every field, the
  session's write and choice rules, and the launch-time reading position and zoom.
- [x] The end-to-end half was run on 2026-10-11 against a portable `qinta.exe`: a seeded
  three-tab session reopened with its active tab, and a normal close wrote it back.

Some filler so there is something to scroll past. Inline `code`, **bold**, *italic*, and a
[link](https://github.com/atabisz/qinta).

```cpp
// The two halves, in src/settings_app.cpp
applySavedPreferences(app, loadSettings());   // at launch
storePreferences(settings, app);             // at close
```

## More text to scroll

Paragraph one of the scrolling filler. A reading position is only worth restoring when there is
somewhere to restore it to, so the document needs to be longer than one window.

Paragraph two. The position is saved per document, along with that document's zoom, and a
document reopened at a different zoom returns to the zoom it was left at.

Paragraph three. The table of contents side and pin, the folder browser pin, heading rules and
zen width are restored the same way.

Paragraph four, the end.

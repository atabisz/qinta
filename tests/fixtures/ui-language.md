# UI language: *German*, French and Italian

Regression fixture for the built-in German, French and Italian UI. Until 2026-10-14 the Qt app
never loaded them: the language list offered **Deutsch**, **Français** and **Italiano**, and
choosing one left every string in English. `languages.ini` was ignored on every platform too.

## By hand

| Step | Expected |
| --- | --- |
| Settings → Language → Deutsch | Settings labels turn German at once ("Einstellungen") |
| Right-click the document | The context menu reads *Kopieren*, *Alles auswählen* |
| macOS: the menu bar | *Ablage*, *Bearbeiten*, *Darstellung*, *Hilfe*, without a restart |
| Settings → Language → Français, then Italiano | The same, in French and in Italian |
| Quit and relaunch | The chosen language is still on |

> A `languages.ini` with `[de]` and `ctx.help=Hilfe (ini)` must show **Hilfe (ini)** in the
> context menu: the file overrides the built-in rows.

- `code`, [a link](https://example.com) and $a+b=c$ are document content and never translate.

```text
A code block stays as written: Settings, Help, Edit.
```

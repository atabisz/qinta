# Start-page dates follow the *region* format

Open this file, close it, and wait until tomorrow — or set the clock forward two
days — so the start page lists it under a date rather than **Today** or
**Yesterday**. The date uses the month abbreviation of the system's **region**
format, not the UI language:

| Region (macOS: Language & Region; Linux: `LC_TIME`) | Expected for 14 September |
| --- | --- |
| English (US or Australia) | `Sep 14` |
| Deutsch (Deutschland) | `Sept. 14` |
| Français (France) | `sept. 14` |
| 日本語 (日本) | `9月 14` |

- The **UI language** in Settings does not change the date. That matches Windows,
  whose arm formats in `LOCALE_NAME_USER_DEFAULT`.
- Recent entries newer than 90 seconds read *Just now*, then *N min ago*, then
  **Today**, **Yesterday** — those strings are translated, not formatted.

> Compare with a [link](https://example.com), `inline code` and $x^2$ in the same
> document, which must lay out as before.

```text
platform::formatMonthDay(2026, 9, 14, "de_DE") == L"Sept. 14"
```

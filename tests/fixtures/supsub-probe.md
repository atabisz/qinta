# supsub

The one document format with no pixel coverage before 2026-09-18: `supSubFormat`, the
0.68x body size that `^sup^` and `~sub~` spans are meant to render at. The three lines
below are a same-glyph comparison on purpose -- identical characters at identical widths,
so a size difference is visible without measuring anything.

XXXX2222 plain

XXXX^2222^ superscript

XXXX~2222~ subscript

THIS FIXTURE PINNED A BUG AND THE BUG IS FIXED, so the paragraph that described it as
current is corrected here rather than left to read as today's behaviour. It said: "On
Windows `app.supSubFormat` is null for the whole run -- `updateTextFormats` creates it,
then `updateOverlayFormats` -> `releaseOverlayFormats` nulls the native before
`seatTextFormats` runs -- so line 2 is pixel-identical to line 1 and line 3 is offset down
at full size."

Three separate things in that sentence are now false. `supSubFormat` is deliberately
absent from `releaseOverlayFormats`, and `include/app.h` says so at the site with the
invariant asserted in `tests/theme_tests.cpp`; `updateOverlayFormats` no longer exists,
having become the shared `installChromeFormats`; and `seatTextFormats` no longer exists at
all, because no text format is seated on either backend any more. So the three lines above
are a same-glyph size comparison that both backends now render alike, which is what the
fixture was built to be able to say.

Editing this paragraph MOVES this page's hash, exactly as the old wording warned -- so the
byte-identity arm for the slice that made these corrections was taken BEFORE this edit, and
the move is a change to the printed text rather than to the renderer.

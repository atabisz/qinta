# supsub

The one document format with no pixel coverage before 2026-09-18: `supSubFormat`, the
0.68x body size that `^sup^` and `~sub~` spans are meant to render at. The three lines
below are a same-glyph comparison on purpose -- identical characters at identical widths,
so a size difference is visible without measuring anything.

XXXX2222 plain

XXXX^2222^ superscript

XXXX~2222~ subscript

WHAT THIS FIXTURE ASSERTS TODAY IS A BUG, and it is here to pin it rather than to bless it.
On Windows `app.supSubFormat` is null for the whole run -- `updateTextFormats` creates it,
then `updateOverlayFormats` -> `releaseOverlayFormats` nulls the native before
`seatTextFormats` runs -- so line 2 is pixel-identical to line 1 and line 3 is offset down
at full size. The Qt path creates the format and never clears it, so the two backends
render this file differently. Fixing that MOVES this page's hash, which is exactly why the
hash is recorded first.

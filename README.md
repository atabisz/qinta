<div align="center">
  <img src="resources/tinta.ico" width="80">
  <h1>Tinta</h1>
  <p><em>Markdown, distilled.</em></p>
  <p>A fast, lightweight Markdown and Mermaid viewer for Windows</p>

  
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT License">
  </a>
</div>

<br>

Tinta is a **fast, lightweight Markdown and Mermaid viewer for Windows**, built with Direct2D and DirectWrite for hardware-accelerated rendering. A single native executable of about 2.3 MB that opens instantly — no Electron, no web engine, no installer.

<p align="center">
  <img src="https://tinta.cc/img/screenshots/paper.png" width="49%" alt="Tinta markdown viewer on Windows — Paper light theme">
  <img src="https://tinta.cc/img/screenshots/midnight.png" width="49%" alt="Tinta markdown viewer on Windows — Midnight dark theme">
</p>

## Install

The recommended way is the [Microsoft Store](https://apps.microsoft.com/detail/9MZ5MZ3L9RKF): the build is signed by the Store, trusted by Windows out of the box (no security prompts, works with Smart App Control), and updates automatically. The same signed build installs from the command line:

```pwsh
winget install "Tinta Markdown Viewer" -s msstore
```

### Portable and other options

Prefer a single file? Grab [the latest release](https://github.com/oipoistar/tinta/releases/latest): a portable `tinta.exe`, no installation required. Because a freshly released exe is not yet code-signed, Windows may show a SmartScreen prompt on first run ("More info", then "Run anyway") until the new version builds up download reputation.

[Scoop](https://scoop.sh) users can install from the bucket:

```pwsh
scoop bucket add tinta https://github.com/oipoistar/scoop-bucket
scoop install tinta
```

## Why Tinta?

Most markdown apps ship an entire browser to render text. Tinta uses the GPU-accelerated text stack already built into Windows:

|  | Tinta | Typora | Obsidian | VS Code |
|---|---|---|---|---|
| Startup | **<100 ms** | ~1.5 s | ~3 s | ~2 s |
| Install size | **~2.3 MB** | ~90 MB | ~250 MB | ~350 MB |
| Runtime | **Native Direct2D** | Electron | Electron | Electron |

It's a viewer first: perfect as the double-click default for `.md` and `.mmd` files, for reading documentation and diagrams — with an edit mode when you need it.

## Features

- **Lightning-fast startup** - Direct2D rendering, no web engine overhead
- **10 beautiful themes** - 5 light and 5 dark themes to choose from
- **Hardware-accelerated** - Smooth text rendering via DirectWrite
- **Word wrap** - Optional soft wrap in the editor (Ctrl+W)
- **Native LaTeX math** - `$inline$` and `$$display$$` equations with matrices, cases, aligned equations, fractions, limits, accents and custom macros. Uses an installed math font; no bundled fonts or web engine. See the [supported math syntax](docs/math-support.md).
- **Focused editing** - Hide the preview pane while writing (Ctrl+E)
- **Native Mermaid diagrams** - 22 diagram families, from flowcharts with subgraphs through sequence, class, state, ER, gantt, and pie to C4, sankey, kanban, and radar - rendered without a web engine
- **Export as HTML, DOCX, or PDF** - Right-click and pick "Export as..." for a self-contained web page, a Word document that opens natively, or a vector PDF; diagrams and math come along in every format
- **Pandoc export** - With [pandoc](https://pandoc.org) installed (or pointed at in settings), the editor offers EPUB, OpenDocument, PowerPoint, RTF and LaTeX too; rich formats convert from Tinta's own HTML export so diagrams and math survive as vector graphics
- **Start page** - Bare launches land on a launcher with recent files, quick actions labeled with your actual key bindings, and built-in guides; closing the last tab returns there, and Ctrl+T opens it as a new tab
- **Review annotations** - Select text and press `A` to attach a note, stored as an invisible HTML comment in the file itself. Annotated passages get a tint and a marker rail beside the scrollbar; hover previews, click edits, and a copy-for-agent button turns the whole review into a paste-ready task list for any coding agent - as the agent fixes items and deletes the comments, the marks vanish live
- **File references** - Plain-text paths like `docs/plan.md` become real links: live targets open as tabs, missing ones render as faded ghosts. Mouse back/forward (or Alt+arrows) walks your jumps with exact scroll restore
- **Link peek** - Rest the pointer on a local `.md` or wiki link and the target appears fully rendered in a panel beside it, no tab opened
- **Image lightbox** - Click any inline image to view it full size: wheel zooms, drag pans, Esc closes
- **Copy tables as TSV** - Hover a table for a copy button that pastes into Excel or Sheets as a real grid
- **Copy diagrams as images** - Hover a diagram for an Image button that puts a crisp 2x PNG on the clipboard
- **Paste screenshots** - Ctrl+V a bitmap in edit mode and it saves as a PNG beside the document with the link inserted
- **Pin on top** - A pushpin in the title bar keeps the window above every other app
- **Rich tables** - Tables with bold, italic, code, and clickable links in cells
- **Tabs** - Win11 Notepad-style tabs in the title bar: files opened from Explorer join the window as tabs, Ctrl+Tab cycles, Ctrl+W or Ctrl+F4 closes (Ctrl+F4 also while editing, where Ctrl+W is word wrap), middle-click closes, Ctrl+T opens the file browser into a new tab, unsaved buffers show a dot, and dragging reorders tabs. Right-click a tab for close operations (close, close all but this, close all to the left/right) plus copy path and reveal in Explorer. Pulling a tab out of the strip floats it as a card: drop it on open space for a new window (which keeps the tab row), or onto another Tinta window to move it there — and dropping a single-file window onto a tab strip merges it back. Single-file windows stay tabless; turn the Explorer behavior off with the "Open files in tabs" setting
- **Folder browser** - Press B to browse and open Markdown or Mermaid files (Ctrl+click a file to open it in a new tab)
- **Table of contents** - Press Tab to see document headings, click to jump; the panel follows your reading position, and typing filters the headings
- **Unified editor** - Press `:` to edit: your raw Markdown on the left with the rendered page floating beside it as a sheet on the editor's desk. A slim line-number gutter, a soft accent wash on the caret's line and its rendered block, a tool rail with formatting controls plus table-size and diagram-template pickers, and Markdown assists (lists continue on Enter, Tab indents, Ctrl+B/I wrap). Search and find-replace work in the editor too
- **Emoji shortcodes** - `:rocket:` and friends render as real emoji
- **Fit to width** - Wide tables and diagrams grow a Fit button that shrinks them to the reading column
- **Draft recovery** - Unsaved quick notes are stashed continuously and offered back on the next launch
- **Create on click** - Clicking a missing file reference offers to create the file and opens it ready to type
- **Text-file references** - `.txt`, `.json`, `.yaml` and friends link like Markdown files and open as highlighted documents
- **Update check** - Portable builds check GitHub once a day and offer a new release as a quiet, dismissible chip
- **Search** - Find text with F or Ctrl+F, cycle through matches with Enter; every match shows as a tick on the scrollbar
- **Persistent settings** - Remembers your theme, zoom level, and window position
- **Portable mode** - Create a `settings.ini` next to `tinta.exe` (an empty file works) and the whole configuration — settings, custom themes, languages, drafts — lives beside the executable and travels with it; without one, everything stays in `%APPDATA%\Tinta`
- **Localized interface** - English, Simplified Chinese, Japanese, Korean, German, French, and Italian UI; follows Windows or a chosen language
- **Text selection & copy** - Select text and copy to clipboard
- **Zoom support** - Ctrl+scroll to zoom in/out
- **Drag & drop** - Drop any Markdown or Mermaid file to view it
- **Minimal footprint** - Small binary, minimal dependencies

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `B` | Toggle folder browser |
| `Tab` | Toggle table of contents (type to filter, Enter jumps) |
| `A` | Annotate the selected text |
| `F` / `Ctrl+F` | Open search |
| `Enter` | Next search match |
| `ESC` | Close overlay / Quit |
| `T` | Open theme chooser |
| `S` | Toggle stats overlay |
| `Ctrl+C` | Copy selected text (or all if none selected) |
| `Ctrl+A` | Select all text |
| `Ctrl+Scroll` | Zoom in/out |
| `Arrow keys` / `J/K` | Scroll |
| `Mouse back/forward` / `Alt+←/→` | Walk link jumps with scroll restore |
| `Page Up/Down` | Page scroll |
| `Home/End` | Jump to start/end |
| `:` | Enter edit mode |
| `ESC` `ESC` | Exit edit mode |
| `Ctrl+N` | New quick note — a fresh window, start typing right away; `Ctrl+S` names it via the save dialog. While the note is still empty, an **Open a file** button (`Ctrl+O`) in the preview pane picks an existing file instead |
| `Ctrl+S` | Save (in edit mode) |
| `Ctrl+Shift+S` | Save As: the editor writes the buffer to a new path, the viewer saves a copy, and the document continues under the new name |
| `Ctrl+H` | Find and replace (in edit mode): Tab switches fields, Enter replaces, Ctrl+Enter replaces all |
| `Ctrl+P` | Print / export to PDF |
| `Ctrl+E` | Show/hide preview pane (in edit mode) |
| `Ctrl+W` | Toggle word wrap (in edit mode) |
| `Ctrl+F4` | Close the current tab (viewer and edit mode) |
| `Q` | Quit |

## Customization

**Shortcut profiles** (Settings → Keyboard shortcuts): pick **Windows**
(`E` edits, `F1` opens help, `F` searches — the default), **Vim** (`/`
searches, `:` edits, `?` helps), or **Custom**. The Edit button opens a
shortcut editor: click an action, press its new key. Raw `:` and `?`
always work regardless of profile.

Settings live in a plain INI file at `%APPDATA%\Tinta\settings.ini` (theme,
zoom, window placement, word wrap, and more — written on exit). The `[Keys]`
section holds the Custom profile's bindings if you prefer editing by hand:
change a value and restart Tinta. Accepted values are letters, digits,
`Tab`, `Space`, `F1`–`F12`, or any single character.

```ini
[Keys]
search=F
browse=B
toc=Tab
theme=T
stats=S
quit=Q
newfile=N
zen=F11
scrollup=K
scrolldown=J
edit=:
help=?
```

Ctrl combos (save, print, copy, word wrap…) are fixed. The help overlay and
context menu show whatever keys are currently bound.

Other settings of note: `browserFocusPath=1` makes `B` open the file browser
with the path box focused and selected, so paste + Enter jumps anywhere
(also in Settings → General). `tinta --new` starts an untitled quick note
from the command line.

The interface language is available in Settings → General. It follows the
Windows display language by default, or can be changed explicitly. Transient
notifications and native error dialogs use the same language. Additional
translations can be added through `languages.ini`; the app remains a native,
single-file Windows viewer with no web runtime.

## Building

Requires Windows with Visual Studio 2019+ and CMake 3.15+.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable will be at `build/Release/tinta.exe`.

This is the Direct2D and DirectWrite build, and it is the one that works today. It is
also the one being replaced — see [the deprecated-backend flag](#the-deprecated-backend-flag)
for what that means and how to measure what still depends on it.

## Cross-platform port (Windows / macOS / Linux)

> Work in progress in this fork. The Windows build documented above still works today
> and is still Direct2D and DirectWrite — but that is the starting point, not the
> destination. Every figure in this section is measured by a script in the repo rather
> than estimated.

### The goal

**One stack on every platform.** Tinta is a Windows app by construction — Direct2D for
drawing, DirectWrite for text, Win32 for windowing — and the goal is to move all three
onto Qt 6 on Windows, macOS and Linux alike, not to keep the native stack on Windows
and bolt a second one on for everywhere else.

That is a deliberate choice against the cheaper option. Keeping Direct2D on Windows and
running Qt only elsewhere would ship sooner, and the seam architecture below would
support it indefinitely. It would also mean two rendering backends, two text engines and
two windowing paths to keep at parity forever — every visual bug reproducible on one
platform and not the other, every feature written twice, and a test suite that can only
ever assert that the two agree rather than that either is right. One stack costs more up
front and less every day after.

So the Direct2D and DirectWrite backends are **transitional**. They are what keeps
Windows working while Qt catches up, and they come out when it has.

The secondary goal turned out to matter more than expected, and it survives the change
of destination intact: a codebase that can be compiled by a second toolchain can also
be *tested* by one. Most of Tinta's logic was untestable in practice because a test
binary needed a window, a Direct2D factory and a device. Behind an abstract interface, a
test can supply a fake — and a fake reaches failure paths that no real backend will
produce on demand. Several of the tests that now exist could not have been written
before the seams did, which is why the seams stay even after the second backend goes.

### Stack choice: Qt 6

Qt 6 (Core + Gui only — not Widgets, not QML) on **all three platforms**, chosen over
Skia after comparing both against what Tinta actually needs:

| | Qt 6 | Skia |
|---|---|---|
| Text shaping and layout | Complete: `QTextLayout`, line breaking, font fallback | Drawing only — shaping is your problem |
| Font enumeration and fallback | Built in | Bring your own (HarfBuzz + fontconfig + …) |
| Windowing, input, IME, clipboard, printing | Built in | Out of scope |
| What Tinta needs most | **Text layout**, which is the hardest part of DirectWrite to replace | GPU drawing, which was never the blocker |

DirectWrite's hard parts are shaping, line breaking and font fallback. Skia does none
of them, so choosing it would have meant assembling a text stack out of three more
libraries. Qt 6 covers all of it and brings the window and input layers too. Qt 6's
open-source build ships Core and Gui under LGPL v3, so a dynamically linked binary can
be distributed alongside an MIT-licensed source tree.

**What Windows gives up, stated plainly rather than discovered later.** Moving Windows
onto Qt trades a native stack for a portable one, and the two costs are real: Tinta's
sub-100 ms start comes partly from having almost nothing to initialise, and Qt is a
larger runtime to load and to ship; and DirectWrite's Windows text rendering is the
reference for how text looks on Windows, so Qt's rendering of the same document will not
be pixel-identical. Neither has been measured on this tree yet — no Qt build of Tinta
exists on any platform — so both are risks to measure at the first runnable Qt binary,
not conclusions. They are the price of the paragraph above, and worth it for the reason
given there.

### How the port is built: seams, not `#ifdef`

There is no `#ifdef _WIN32` sprinkled through the drawing code. Each platform
dependency becomes a small abstract interface with a header per backend. The
"transitional" column is what keeps Windows working today and is scheduled for deletion;
the "target" column is where every platform ends up:

| Seam | Replaces | Transitional (Windows, to be removed) | Target (all platforms) |
|---|---|---|---|
| `include/text_api.h` | DirectWrite text layout and formats | `src/text_d2d.cpp` | `src/text_qt.cpp` |
| `include/render_api.h` | Direct2D render target, brushes, bitmaps | `src/render_d2d.cpp` | `src/render_qt.cpp` |
| `include/win32_types.h` | Win32 scalar types (`UINT32`, `BOOL`, `FLOAT`, …) | real `<windows.h>` types | stand-in typedefs, each pinned to the SDK's by `static_assert` — and eventually neither, once no code names them |
| `include/win32_api.h` | Win32 and CRT *calls* (`swprintf_s`, `PostMessage`, …) | native | portable spellings |
| `include/platform_paths.h` | Config and data directory lookup | `src/platform_paths_win32.cpp` | `src/platform_paths_posix.cpp`, then `QStandardPaths` |
| `include/seated.h` | How `App` *stores* a backend object | — | `Seated<Seam, Native>` |

The seams themselves are **not** transitional, even though a single-stack app does not
strictly need an abstraction over its one backend. They stay because they are what makes
the code testable without a device: a fake layout is a class in a test header instead of
a factory, a window and a GPU. Collapsing them into direct Qt calls to "simplify" would
buy nothing and cost the fixture that 400-plus assertions run on.

`seated.h` is the one that is easy to underrate. Rewiring call sites onto an interface
does not make a type portable while the type still *names* a backend as a member —
`App` held the Direct2D wrappers by value, so linking anything that constructed an
`App` demanded vtables from the Windows-only backends. `App` now holds a `unique_ptr`
to the abstract seam plus the native it was built from, and that single change is what
made testing files that take an `App&` possible at all.

### Building and testing off Windows

Requires g++ 13+, CMake 3.15+, and Qt 6 for the Qt-backed targets. Verified on Ubuntu
24.04 under WSL with g++ 13.3.0, CMake 3.28.3 and Qt 6.4.2. Nothing here has been built
with clang or on macOS — that is untested, not known-good.

```bash
sudo apt install g++ cmake qt6-base-dev

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
(cd build-linux && ctest --output-on-failure)
```

Qt is currently **opt-in**: CMake looks for it with
`find_package(Qt6 QUIET COMPONENTS Core Gui)` and simply skips the `qt_text` and
`render_api` targets when it is absent, which is what happens on Windows today. That is
transitional too — under one stack Qt becomes a hard requirement on Windows as well, and
making it one is a step that has not been taken because no Qt is installed on the Windows
machine this is developed on. Those two tests run headless and set
`QT_QPA_PLATFORM=offscreen` themselves — it is not interchangeable with `minimal`, and
running with no platform plugin at all aborts under WSL.

There is **no CI**, so nothing is green unless somebody ran it. The regression guard is
a probe that compares what the build actually compiled against a manifest of what it is
supposed to compile, in both directions:

```bash
bash scripts/probe-portable-linux.sh
```

It fails if a file in `tests/portable-sources.txt` was *not* compiled (something dropped
out of a portable target — a silent regression) or if a file was compiled that the
manifest does *not* list (the portable surface grew without anyone saying so). It also
prints the portable share, computed from the manifest on every run rather than quoted
from anywhere. Run it after any change that touches which files build off Windows.

### The deprecated-backend flag

`TINTA_LEGACY_D2D` marks the Direct2D and DirectWrite backends as transitional in the
build itself, so that "scheduled for deletion" is a CMake option rather than a sentence in
a README. It **defaults `ON`**, and with the default nothing changes: Windows builds and
tests exactly as before.

```bash
cmake -S . -B build-nod2d -DTINTA_LEGACY_D2D=OFF
cmake --build build-nod2d --config Release   # expected to FAIL, and that is the point
```

Turning it off does not produce a Qt build of Windows — it produces no Windows build at
all, because the app still reaches Direct2D through the seam and nothing else implements
it yet. So the flag's job today is not to be flipped. It is to be a **measuring
instrument**: the build failure it produces is an exact inventory of what still depends on
the stack being replaced. Measured on 2026-09-15:

| Reading | Value |
|---|---|
| Compile errors (`error C`) | **0** — nothing outside the two backend files *names* Direct2D |
| Targets failing to link | 7 |
| Unresolved externals | 46 (16 for `math_layout_tests`, which links less) |
| Classes they belong to | `D2DTextLayout`, `D2DTextFormat`, `D2DRenderTarget`, `D2DBitmap`, `D2DTextFactory`, `D2DBrush` |

Zero compile errors is the useful half of that. It means the seam rewiring is real and
complete at the call sites: the remaining Windows debt is six wrapper *implementations*,
not Direct2D calls scattered through the app — and it is the same six a Qt backend already
provides on Linux.

Three test targets are gated on the flag and disappear when it is off, because their
subject *is* the backend: `render_d2d_tests`, `bitmap_seam_tests`, `text_factory_tests`.
Everything else that links the backends incidentally is deliberately **not** gated. Those
targets' subjects survive the stack change, so under `OFF` they are *supposed* to fail to
link — gating them would hide the very inventory the flag exists to print.

Flipping the default requires Qt on Windows, which is not installed on the machine this is
developed on. That is an install and a decision, not a CMake change.

### Where the two stacks will not agree

Some behaviour cannot be identical on both backends, and the position taken here is to pick
one, write it down, and assert it in a test rather than let it be discovered later as a bug.
The tests carry these as per-backend expectations, so a future change that accidentally
"fixes" one of them fails loudly.

- **Discretionary ligatures in code blocks.** The DirectWrite path applies `dlig`; the Qt
  path does not, because Qt 6.4's `QTextCharFormat` has no per-feature API and
  `QFont::setFeature` only arrived in 6.7. Standard ligatures, kerning and contextual
  alternates (`liga`, `kern`, `calt`) work on both. In practice this affects a handful of
  glyph pairs in fonts like Fira Code. `qtCanHonourFeature` is the single place that decides
  it, and it is a **whitelist rather than a `dlig` special case**, so a feature added later
  is refused until someone states that Qt can honour it — the test asserts that too, using
  `swsh`. The test runs 37 assertions on Windows and 34 off it; the three extra are not
  about `dlig` but are cross-checks that the portable tag packing equals the DirectWrite SDK
  macros, which can only be compiled where the SDK exists.
- **Line breaking on a lone carriage return, and on U+2029.** Qt does not break where
  DirectWrite does. Both are deliberately left unasserted for now — they need the line
  breaking seam that `render.cpp` still lacks, and asserting a behaviour before the seam
  exists would pin the wrong thing.

### Status

Measured on 2026-09-15 at this commit, Windows and Linux both built and tested in the
same session.

**Done**

| Area | State |
|---|---|
| Markdown parsing, Mermaid parsing (13 files), themes, i18n, geometry, math parsing, selection and selection layout, context menus, inline styles, settings round-trip, keymap, platform paths, portable strings, syntax highlighting, OpenType typography | Compile *and* assert off Windows — 18 test targets |
| Text seam (`text_api.h`) + Qt backend | Done, tested headless |
| Render seam (`render_api.h`) + Qt backend | Done, tested headless |
| Win32 scalar + call seams | Done — 5 scalars admitted from an error census (`UINT32`, `FLOAT`, `BOOL`, `TRUE`, `FALSE`), 6 more refused for appearing in no error at all, and `HRESULT`/`SUCCEEDED` withheld deliberately so COM-shaped code cannot quietly compile off Windows |
| Platform paths seam | Done, POSIX backend |
| Theme data de-Direct2D'd | Done — colours are no longer `D2D1_COLOR_F`, which is what freed `themes.cpp` (511 lines of palette data and an ini reader) from `windows.h` |
| `App` storage behind the seam (`seated.h`) | Done — an `App` links and runs off Windows with zero Direct2D wrapper symbols in the binary |
| Synthetic `App` layout for tests (`tests/app_fixture.h`) | Done — a fake, not a device |
| Portable-build regression guard | Done — manifest + probe, both directions |
| Tests running off Windows | **20 run on Linux, 18 of them shared with the 28 that run on Windows.** The other 2 are the Qt backend tests, which exist *only* off Windows. 30 distinct test targets in total |
| Text path in `render.cpp` on the seam | Done — every layout the file creates goes through `app.textFactory`, and `dwriteFactory` is named at **zero** live sites. Typography is a value, not a COM object. Honest limit: **no test links `render.cpp`**, so the build is the evidence here and the test count is not |

**To do — 24 files, 30,122 lines**

| Area | Size | Note |
|---|---|---|
| `render.cpp` | 3,498 | **Still the blocker, but no longer for text.** Native mentions went 128 → 93, and live ones — excluding comments — 115 → 75; the *text* subset fell 56 → 16. Every layout now goes through `app.textFactory` and `dwriteFactory` is named at zero live sites, so paragraphs, wrapped labels and diagram text all lay out off Windows. What remains is four other seams: path-geometry arcs (22 live mentions), WIC image decoding (10), line breaking via `IDWriteTextAnalysisSource`/`Sink` (6), `ID2D1SvgDocument` (3), plus the `IDWriteTextLayout2` font-fallback query (2), which is deliberately *not* pushed into the backend — see the note below the table. The residual `ID2D1`/`D2D1_` count is unchanged at 16/33, because drawing is a different seam and this work did not touch it |
| Window and message loop | 4,348 + 2,273 | `input.cpp`, `main_d2d.cpp` — Win32 message pump → Qt event handling |
| Editor | 3,138 | `editor.cpp`, including IME input via `imm.h` → Qt input methods |
| Overlays, annotations, math, table edit | 4,109 + 1,194 + 848 + 616 | No Windows headers left; blocked on the layout seam above and on function-local Direct2D construction |
| Exports and printing | 1,547 + 1,500 + 998 + 228 | `commdlg.h` → `QFileDialog`, and printing → `QPrinter` |
| Shell integration | 1,719 + 836 + 160 | `shellapi.h` → `QDesktopServices` |
| Clipboard, paths, misc | 198 + 158 | `utils.cpp` → `QClipboard` and `std::filesystem` |
| Startpage | 792 | `dwrite_1.h`; also uses `SetCharacterSpacing`, which stays native |
| Device and factory creation | 674 | `d2d_init.cpp` — `objbase.h`, COM initialisation and the Direct2D device; becomes Qt paint-device setup |
| Keyboard seam | — | Designed, not built: 17 `GetKeyState` sites, two keys, one predicate → `QGuiApplication::keyboardModifiers()` |
| Qt entry point | — | `Seated::install()` still has zero callers; blocked behind `render.cpp` |
| macOS | — | Not attempted. Nothing platform-specific to macOS has been written or measured |
| The Windows backends themselves | 219 + 596 + 212 + 146 + 65 | `render_d2d.cpp`, `text_d2d.cpp`, `settings_win32.cpp`, `platform_paths_win32.cpp`, `settings_app.cpp` — **deleted or replaced, not ported.** Registry access becomes `QSettings`, path lookup becomes `QStandardPaths`, and the two backends go once Qt covers what they do |

The font-fallback query is the one place where the obvious tidy-up is the wrong move, and
it is worth a note because the reasoning is not visible from the code. Folding
`SetFontFallback` into the backend's `CreateTextLayout` would make it invisible to every
caller — and would change behaviour in files nobody touched. Fifteen sites already create
layouts through `app.textFactory` and none has ever had a fallback, which
`git log -S'SetFontFallback'` confirms across the whole history; several of them *measure*
text, so giving them a fallback would move measured widths and therefore layout. It stays
in `render.cpp` as one named helper, which also makes it the single line to delete if that
answer ever changes.

That last row is the change of goal made arithmetic. These five files used to be listed
separately as "will never move", excluded from the count on the grounds that they were
the Windows half of a permanent pair. Under one stack there is no permanent Windows half,
so they are work — 1,238 lines of it — and they are counted.

Ten test targets still run only on Windows. Under the previous goal four of them always
would, being Windows-only by *subject*: `render_d2d` and `text_factory` assert on the
Direct2D and DirectWrite objects themselves, `bitmap_seam` asserts on the Direct2D bitmap
wrapper, and `config_paths` deliberately includes `windows.h` to check how config lookup
copes with MSIX filesystem virtualization. Under one stack the first three go with the
backends they test, and only `config_paths` clearly survives — MSIX is a Windows
packaging fact and stays true whatever draws the window. The remaining six
(`blockquote_layout`, `code_block_layout`, `math_layout`, `inline_code_theme`,
`copy_file_path`, `input_shortcuts`) are blocked behind the files above, mostly behind
`render.cpp`.

### How complete is it?

A single number would be misleading here, because "compiles", "is tested" and "runs"
are three different milestones and the port is at very different points on each. Every
basis below is measured — Windows and Linux each built and tested in the same session:

| Basis | Measured | |
|---|---|---|
| A. Source lines free of the Windows stack | 20,439 / 57,253 | **35.7%** |
| B. `src/*.cpp` files compiling without Windows headers | 33 / 57 | 57.9% |
| C. Files a *running* test links | 30 / 57 | 52.6% |
| D. Windows tests that also run off Windows | 18 / 28 | 64.3% |
| E. All test targets, run off Windows | 20 / 30 | 66.7% |
| F. **Platforms running Tinta on the target stack** | 0 / 3 | **0%** |

**Basis A, 35.7%, is the headline** — it is the figure the probe itself prints on every
run, so it is the one comparable across time. Three honest caveats on it.

It *understates* progress, because its denominator includes headers that the manifest
never credits even when they are portable. It *overstates* readiness, because three of the
33 compiling files (`search.cpp`, `editrail.cpp`, `i18n_app.cpp`) are compile-only:
nothing executes them on either platform, so they are proof that a compiler accepts them
and nothing more.

And it just moved the *wrong way* on a commit that was real progress: 35.8% → 35.7%, while
`render.cpp` lost 40 live DirectWrite text mentions and gained a working seam. The reason
is arithmetic, not regression. Basis A counts lines in portable *files*, so putting a
not-yet-portable file's text path on the seam credits nothing until the whole file
compiles — and the explanatory comments that came with the change made the denominator
bigger. A metric that can fall on a good commit is worth keeping anyway, because it is
honest about what it measures, but it is not the metric to read for whether the port is
progressing. The `render.cpp` row in the table above is.

A basis was **removed** rather than updated when the goal changed. There used to be a
line reading "same, excluding the five never-move files — 36.6%", and it measured
something that no longer exists: under one stack nothing is excluded, because the Windows
backends are work rather than furniture. A percentage whose denominator quietly stops
meaning anything is worse than no percentage, so it is gone rather than adjusted.

Basis F is the one to keep in view, and the change of goal made it stricter: it used to
ask whether a non-Windows binary existed, and now asks how many of the three platforms
run Tinta on Qt. **The answer is none.** Windows runs, on the stack being replaced.
`render.cpp` — 3,498 lines and 93 native Direct2D and DirectWrite mentions, 75 of them
live — is the single file standing between "the library compiles and is tested" and "it
runs anywhere on Qt", and it is the same blocker under either goal. It is a *smaller*
blocker than it was: text layout came off DirectWrite entirely, and the four seams left in
it are drawing and decoding rather than typesetting. That is the good news in this section,
together with the fact that going single-stack did not add a blocker — it removed the
obligation to maintain two.

Call it roughly a third of the way through by volume, with the hardest single file still
ahead — and note that the finish line moved out, not in: 1,238 lines of Windows backend
became work, and re-verifying Windows on a new stack is effort that appears in no line
count here.

## Usage

```bash
# Open a Markdown file
tinta.exe document.md

# Open a Mermaid flowchart
tinta.exe diagram.mmd

# Open with light theme
tinta.exe -l document.md

# Show stats on startup
tinta.exe -s document.md

# Register for Markdown and Mermaid files
tinta.exe /register
```

Or simply drag and drop a `.md` or `.mmd` file onto the window.

To copy a document's full path, right-click the document or its tab (the filename
in the title bar when only one file is open) and choose
**Copy file path**, beside **Reveal in Explorer**. The tab menu copies the clicked
tab's path, even if another tab is active. Untitled notes have no path until saved.

## File Association

On first launch, Tinta will ask if you want to set it as the default viewer for Markdown and Mermaid files. If you choose "No", you won't be asked again.

To register Tinta as the default viewer later, run:
```bash
tinta.exe /register
```

This registers `.md`, `.markdown`, and `.mmd` so you can select Tinta as their default app in Windows Settings.

## Mermaid Support

Tinta natively renders twenty-two Mermaid diagram families in `.mmd` files and fenced `mermaid` code blocks — no web engine involved:

- **Flowcharts** (`flowchart` / `graph`) - TB/TD, BT, LR, RL layouts, `subgraph` groups, common node shapes, directed and labeled edges, `classDef`/`class`/`style` styling, and graceful handling of Mermaid 11 edge IDs and node configs
- **Sequence diagrams** - participants and actors with aliases and `<br/>` breaks, every arrow family, activations, notes, `autonumber`, and `loop`/`alt`/`opt`/`par`/`critical`/`break` frames
- **Class diagrams** - member blocks, stereotypes, `~T~` generics, all relation kinds with multiplicities, `note` and `note for`
- **State diagrams** - `[*]` start/end, choice/fork/join, descriptions, directions, inline notes
- **ER diagrams** - attribute tables with keys, full crow's-foot cardinality notation
- **C4 diagrams** (`C4Context`/`C4Container`/`C4Component`/`C4Dynamic`/`C4Deployment`) - persons, systems, containers, and components in the standard C4 palette, nested boundaries, labeled relations
- **Requirement diagrams** - typed requirement and element boxes with labeled relations
- **Architecture diagrams** (`architecture-beta`) - services with line-art icons, groups, junctions, and side-anchored edges
- **Block diagrams** (`block-beta`) - column grids, spans, spaces, nested blocks, block arrows, labeled edges
- **Sankey diagrams** (`sankey-beta`) - ranked nodes with smooth flow ribbons
- **Packet diagrams** (`packet-beta`) - 32-bit rows with labeled bit-range fields
- **Kanban boards** - columns and cards with ticket, assignee, and priority metadata
- **Treemaps** (`treemap-beta`) and **radar charts** (`radar-beta`)
- **Pie charts**, **git graphs**, **gantt charts** (including the numeric `dateFormat X` axis), **mindmaps**, **timelines**, **user journeys**, **quadrant charts**, and **XY charts** (`xychart-beta`)

Diagrams follow the active theme, print through Ctrl+P, export as SVG in HTML and as crisp images in DOCX, and their text is selectable and searchable like any other content. Anything a native renderer does not cover yet (composite states, zenuml) falls back to readable source code.

## Themes

Press `T` to open the theme chooser:

**Light Themes:**
- Paper - Warm sepia, literary feel
- Sakura - Soft pink elegance
- Arctic - Cool blue-white
- Meadow - Fresh green
- Dusk - Warm gray twilight

**Dark Themes:**
- Midnight - Deep blue-black
- Dracula - Purple-tinted dark
- Forest - Deep green
- Ember - Warm charcoal
- Abyss - Pure black (OLED-friendly)

Custom themes live in `themes.ini` beside the portable executable, or in
`%APPDATA%\Tinta`. Add `inlinecode` to a `[theme]` section to color inline code
independently of fenced blocks:

```ini
code=334455
inlinecode=B00020
```

Here inline code is red and plain fenced code stays neutral. Syntax highlighting
keeps its own colors. Without `inlinecode`, inline code uses `code` as before.
The override is preserved when saving a theme from the editor and applies to
HTML and DOCX exports. Native printing keeps Tinta's existing light print palette.

## Dependencies

- [MD4C](https://github.com/mity/md4c) - Fast markdown parser (fetched automatically by CMake)
- Windows Direct2D/DirectWrite (system libraries)

## License

MIT

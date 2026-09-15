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
| `include/platform_paths.h` | Config and data directory lookup | `src/platform_paths_win32.cpp` | `src/platform_paths_qt.cpp` — `QStandardPaths`/`QDir`/`QLocale`, and it **replaced** `platform_paths_posix.cpp` rather than joining it |
| `include/keyboard.h` | "is a modifier held" (`GetKeyState`) | `src/keyboard_win32.cpp` | `src/keyboard_qt.cpp` |
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
`find_package(Qt6 QUIET COMPONENTS Core Gui)` and skips the targets that need it when it is
absent. **Two sentences that used to follow here said Qt was absent on Windows and that
making it a hard requirement was blocked on installing it. Both are now false and are
corrected rather than deleted**, because the shape of the error is the useful part: they were
true when written, the pass that installed Qt 6.8.3 on Windows updated the figures it touched
and left the prose two sections away standing, and a stale *capability* claim reads as a
considered limitation forever. Qt is installed on the Windows machine this is developed on,
and Windows builds every Qt target including the frontend. What still blocks the requirement
becoming hard is not an install — it is the twelve build failures in the next section.

The headless targets pin `QT_QPA_PLATFORM` themselves, and it is pinned **per platform, not
once**: `offscreen` off Windows, `windows` on Windows, from one `TINTA_QPA_PLATFORM` variable
at `CMakeLists.txt:1524`. That asymmetry is measured, not stylistic — the offscreen plugin on
Windows has **no font database**, so a text assertion there measures a fabricated metric
rather than a glyph. `minimal` is used nowhere, for the same reason and more so, and running
with no platform plugin at all aborts under WSL.

```bash
# the Qt frontend, on either platform
cmake --build build-linux --target tinta_qt
QT_QPA_PLATFORM=offscreen ./build-linux/tinta_qt      # headless: starts, paints, no window

# on Windows, where Qt is not on the default search path
cmake -S . -B build-qtwin -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build-qtwin --target tinta_qt --config Debug
```

`qt_text`, `render_api` and `qt_entry` exist *only* where Qt does. `keyboard` is the exception and
deliberately so: it is configured under `if(WIN32 OR Qt6_FOUND)`, because the seam has two
backends and a seam whose Windows half is never asserted is precisely the shape this repo
has criticised elsewhere. Off Windows it links `keyboard_qt.cpp` and Qt; on Windows it links
`keyboard_win32.cpp` and no Qt at all.

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

### The Qt frontend, and the twelve reasons it shows no document

`tinta_qt` is a real target on both platforms: a `main()`, a `QWidget` subclass, and the
first callers `Seated<>::install()` has ever had. It **starts and stays running** —
`timeout 5 tinta_qt` reads exit `124` on Windows and on Linux, meaning the timeout had to
kill it, so `main()` reached `QApplication::exec()`. The control matters more than the
reading: `qt_entry_tests` under the same `timeout` reads `0`, so `124` means *alive* rather
than being what this harness returns for everything.

Its source list is nine files, and it is the **measured link closure** rather than a guess —
read off a `-Wl,-Map=` link map, not inferred from includes. Six of the 39 portable objects
are in it (`config_dir`, `markdown`, `platform_paths_qt`, `themes`, `render_qt`, `text_qt`),
plus the three new ones. **md4c is on that link line even though `tinta_qt` parses nothing**,
because `App` holds a `qmd::MarkdownParser` *by value* — constructing an `App` references its
constructor, which references `md_parse`. That is the third time a linker has taught this
tree that fact.

What it draws is one centred line of placeholder text, and the honest statement of why is a
list rather than a sentence. **Of the 24 `src/*.cpp` files the manifest does not list, 23 fail
to compile off Windows, from 12 distinct causes** — measured with `-fsyntax-only` at
`census_exit=0` against a control, reading `fatal` before any error tally, because a file that
dies on its first include reports `errors=1` and that means *not measured* rather than
one-error-away:

| Cause | Files | |
|---|---|---|
| `windows.h` | 5 | `image_win32`, `keyboard_win32`, `platform_paths_win32`, `settings_win32`, `utils` |
| `windowsx.h` | 2 | `input`, `main_d2d` |
| `shlobj.h` | 2 | `drafts`, `pandoc` |
| `shellapi.h` | 2 | `signals`, `tabs` |
| `objbase.h` | 2 | `d2d_init`, `export_docx` |
| `commdlg.h` | 2 | `file_dialog_win32`, `print` |
| `imm.h` | 1 | `editor` |
| `dwrite_1.h` | 1 | `startpage` |
| `wcscpy_s` not declared | 1 | `overlays` |
| `IDWriteTextLayout2` not declared | 1 | **`render.cpp`** |
| wide-path `ofstream` constructor | 1 | `annotations` |
| incomplete `IDWriteTextLayout` | 1 | `math_render` |

21 files, plus the two deliberate `#error` guards in `render_d2d.cpp` and `text_d2d.cpp`, is
the 23. The 24th is `settings_app.cpp`, which **compiles off Windows already** and is a
deliberate manifest exclusion rather than a Windows dependency — worth knowing before someone
spends a pass "porting" it.

The single most informative row is `render.cpp`. It no longer dies on `windows.h` at all; its
first error is a missing DirectWrite *interface*, which is measurable evidence that the header
work landed. It is also the file every other blocker routes through: give `render.cpp` a
portable compile and the Qt window has a document to draw.

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
| Targets failing to link | 7 (`tinta`, `blockquote_layout`, `code_block_layout`, `file_path`, `input`, `math_layout`, `theme`) |
| Unresolved externals | 47 (17 for `math_layout_tests`, which links less) |
| Classes they belong to | `D2DTextLayout`, `D2DTextFormat`, `D2DRenderTarget`, `D2DBitmap`, `D2DTextFactory`, `D2DBrush` |

That external count read **46** here for two commits, and correcting it is worth a sentence
because of *how* it went wrong. The table carries a measurement date, and the date was
refreshed on a commit that did not re-run the measurement — so a stale number sat under a
current date, which is worse than sitting under an old one. The 47th symbol is
`D2DTextLayout::SetTypography`, added when typography moved onto the text seam; the seam
grew a method, so excluding the backend leaves one more thing unresolved. Re-run the two
commands above rather than trusting this table, and if you refresh the date, refresh the
numbers in the same pass.

Zero compile errors is the useful half of that. It means the seam rewiring is real and
complete at the call sites: the remaining Windows debt is six wrapper *implementations*,
not Direct2D calls scattered through the app — and it is the same six a Qt backend already
provides on Linux.

Three test targets are gated on the flag and disappear when it is off, because their
subject *is* the backend: `render_d2d_tests`, `bitmap_seam_tests`, `text_factory_tests`.
Everything else that links the backends incidentally is deliberately **not** gated. Those
targets' subjects survive the stack change, so under `OFF` they are *supposed* to fail to
link — gating them would hide the very inventory the flag exists to print.

**Flipping the default no longer requires an install, and this sentence used to say it did.**
Qt 6.8.3 is on the Windows machine and `tinta_qt` builds there, so the missing piece is not a
package — it is that `tinta_qt` draws a placeholder and `tinta.exe` draws documents. Flipping
the default today would trade a working viewer for a window with one line of text in it. The
inventory above is what stands between the two, and it is a build failure list rather than a
decision.

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

### The open product decision: the Windows C runtime

This one is not a portability question and it is not mine to settle, so it is written down
rather than decided. `CMakeLists.txt:20-22` links the whole tree against the **static** CRT,
on a stated promise: the exe runs on a clean Windows install with no VC++ redistributable,
which is what keeps Tinta a single file you can copy. **Qt cannot be linked against the
static CRT.** So the moment `render_qt.cpp` and `text_qt.cpp` stop being a tested seam and
become the shipping backend, `tinta.exe` faces the choice the two Qt *test* targets already
had to make, and the single-file promise is what pays for it.

The options are a dynamic CRT plus a redistributable dependency, shipping the CRT DLLs
alongside the exe, or a static Qt build — and each trades something real.

**This got sharper rather than staying hypothetical: `tinta_qt` is now a Windows target that
is not a test, and it carries the dynamic-CRT override.** Until this pass the override touched
only `qt_text` and `render_api`, which nobody ships, so the promise at `CMakeLists.txt:20-22`
was intact for every product binary. There are now two Windows executables built from one tree
under two different C runtimes, and the one that is the future of the stack is the one that
cannot keep the promise. Nothing breaks today — `tinta.exe` is unchanged and still static —
but the decision is no longer ahead of the port, it is inside it, and the first person to ship
a Qt Windows build makes it whether they mean to or not.

### Status

Measured on 2026-09-16 at this commit, Windows and Linux both built and tested in the
same session.

**Done**

| Area | State |
|---|---|
| Markdown parsing, Mermaid parsing (13 files), themes, i18n, geometry, math parsing, selection and selection layout, context menus, inline styles, settings round-trip, keymap, platform paths, portable strings, syntax highlighting, OpenType typography, in-document search, the modifier-key predicate | Compile *and* assert off Windows — 20 test targets |
| Text seam (`text_api.h`) + Qt backend | Done, tested headless |
| Render seam (`render_api.h`) + Qt backend | Done, tested headless |
| Win32 scalar + call seams | Done — 5 scalars admitted from an error census (`UINT32`, `FLOAT`, `BOOL`, `TRUE`, `FALSE`), 6 more refused for appearing in no error at all, and `HRESULT`/`SUCCEEDED` withheld deliberately so COM-shaped code cannot quietly compile off Windows |
| Platform paths seam | Done — **and the POSIX backend is now DELETED rather than maintained.** `src/platform_paths_qt.cpp` replaced all 334 lines of `platform_paths_posix.cpp`, including a hand-rolled UTF-8↔`wchar_t` codec and a double-`fork`/`execlp("xdg-open")`/`waitpid` sequence whose whole purpose was avoiding a zombie process. Nine of the eleven functions have no platform branch at all in the new file, which is what one stack on three platforms looks like at the file level; the two that do are `executableDirectory` (every Qt answer needs a `QCoreApplication` instance and the callers run before one exists) and `documentPathsEqual` (Qt has no per-platform path-identity rule, so the shipped comparison is preserved verbatim). Written against measurements taken *first*, on Qt 6.4.2 and 6.8.3, which is the only reason it is correct: four defects were designed out, three of them silent — most seriously `QStandardPaths::GenericConfigLocation` being `%LOCALAPPDATA%` on Windows, which would have relocated every existing user's `settings.ini` out of Roaming and orphaned the old one behind a clean-looking diff |
| Theme data de-Direct2D'd | Done — colours are no longer `D2D1_COLOR_F`, which is what freed `themes.cpp` (511 lines of palette data and an ini reader) from `windows.h` |
| `App` storage behind the seam (`seated.h`) | Done — an `App` links and runs off Windows with zero Direct2D wrapper symbols in the binary |
| Synthetic `App` layout for tests (`tests/app_fixture.h`) | Done — a fake, not a device |
| Portable-build regression guard | Done — manifest + probe, both directions |
| Tests running off Windows | **27 run on Linux, and all 27 also run on Windows, out of 37 there.** 37 distinct test targets in total, both figures re-derived from `ctest -N` this pass rather than incremented — Linux prints 28 *entries* against 27 targets, which is the one place a `+1` looks right and is wrong. The new target is `qt_entry`, and "all 27" is a measurement: the two `ctest -N` listings were diffed, and the only entry Linux has that Windows does not is `platform_paths_bare_locale`, a second entry for a target Windows already runs. The "Linux-only" category is now **empty**, and that is a real change rather than a rounding: the Qt backend tests used to exist only off Windows, and installing Qt 6.8.3 on Windows means the Qt backend is now asserted on both platforms — so `render_api`'s assertions run three times, Qt on Linux, Qt on Windows, and Direct2D on Windows |
| Text path in `render.cpp` on the seam | Done — every layout the file creates goes through `app.textFactory`, and `dwriteFactory` is named at **zero** live sites. Typography is a value, not a COM object. Honest limit: **no test links `render.cpp`**, so the build is the evidence here and the test count is not |
| In-document search asserted off Windows | Done — `search.cpp` was compile-only on both platforms and now has 53 assertions running on each. The blocker was never its shape: a real link found ten undefined symbols, nine already in portable files and one (`ensureLayoutComplete`) still in `render.cpp`, which the test stubs. Honest limit: **that stub means the precondition itself is untested**, and two folder-scan functions are unreachable from another translation unit and left uncovered |
| The App overload of `tr()` asserted off Windows | Done — `i18n_app.cpp` is one forwarding line and was the second of the two compile-only files. It now runs 42 assertions inside the existing `i18n` test, on both platforms, taking that test from 40 to 82. Worth doing for the failure mode rather than the size: its whole job is to read `app.currentLanguageIndex`, and a version that forwards a constant leaves the app **silently monolingual** — no error, no crash, every string still drawn. A planted constant fails 34 of the 42. Honest limit: this adds **no new test target**, so the revert is invisible to the manifest, the ctest count *and* the pass rate — the only instrument is a new check on the link line |
| Modifier-key seam (`keyboard.h`) + both backends | Done — and the interesting part is what made it *testable*. The seam's own two calls can only be asserted to return false, because no key is held while a test binary runs, and a stub that never reads the keyboard passes that too. So the decision every call site actually depends on was split out as a pure predicate: `modifierHeld(short)` is `(state & 0x8000) != 0`, and the realistic bug is reading the neighbouring low bit instead, which is the sticky Caps-Lock-style toggle — that would make Shift+Tab depend on whether Caps Lock was on. Being pure, it is asserted over its **entire 65,536-value domain** on both platforms, and against an independent characterisation (bit 15 set ⇔ the `short` is negative) rather than against its own expression, so a restatement could not pass — a wrong-bit implementation disagrees on exactly half the domain. 65,545 assertions, identical on both platforms because every one of them is arithmetic. It is also the Windows backend's *own* decode step, so those are a Linux run asserting the expression Windows depends on. What stays unprovable without a human holding a key is now just the syscall, not the seam. `QGuiApplication::keyboardModifiers()` and *not* `queryKeyboardModifiers()`: all 17 sites read event-time state, and the wrong one of those compiles, runs, and is wrong only while keys are moving |
| `tableedit.cpp` off Windows | Compiles — 621 lines, 12 g++ errors to 0, and **every one of the 12 was a keyboard error**: 10 missing `VK_` codes and the 2 `GetKeyState` calls. Nothing else in the file names a Windows API. Honest limit, and it is the whole reason this is a separate row from the one above: **nothing executes it on either platform.** It is the second file in that state |
| Image seam (`image_api.h`) + both backends | Done — remote fetch, raster decode and SVG rasterization, `urlmon`/WIC/`d2d1_3` on one side and `QNetworkAccessManager`/`QImage`/`QSvgRenderer` on the other. It is what took `render.cpp`'s three unguarded headers, so it is the reason that file can be measured off Windows at all. **56 assertions, the same 56 on both platforms**, against a 2×2 PNG the test emits byte by byte — table-less CRC32, hand-rolled adler32, one stored deflate block, so no zlib and every expected value hand-computable. The pixels are chosen as discriminators rather than as data: an opaque red separates BGRA from RGBA, and a **white pixel at alpha 0** separates premultiplied from straight alpha, because 255 × 0 is 0 while a straight decode leaves 255,255,255 sitting there. Ten planted controls, five per backend, each with its failure count predicted before the run and each matching exactly. Honest limits, all three stated at their own sites: **no test makes a real HTTP request**, so only the fetch's failure path is asserted; the fixture cannot separate row *padding* from tight packing, because at 32bpp a `QImage` scanline is already 4-byte aligned, so `image_qt.cpp`'s row-by-row copy rests on an unasserted argument; and **nobody has looked at the running viewer** |
| Path geometry seam (`PathSink`/`PathGeometry`) + both backends | Done — and the interesting half is arithmetic rather than plumbing. Direct2D describes an arc by its **endpoint** (`D2D1_ARC_SEGMENT`); Qt describes it by a bounding **box** plus a start angle and a sweep in degrees, counter-clockwise-positive. Neither converts to the other by renaming fields, so somebody has to solve for the centre — and that solve lives in `include/geometry.h`, not in the Qt backend, precisely so a **Windows** build compiles it too and both compilers assert it. All tinta coordinates are y-down, so an increasing `atan2` angle is clockwise on screen, which is what `D2D1_SWEEP_DIRECTION_CLOCKWISE` also means: the Qt side therefore negates *both* the start angle and the sweep. The seam **mirrors the D2D sink call sequence** rather than describing shapes, and that is a measured decision, not a stylistic one — `render.cpp`'s slice builder special-cases a full circle as two half-arcs and `export_docx.cpp`'s does not, so a `CreateSlice(centre, radius, a0, a1)`-shaped seam would have had to pick one and would have **silently changed what an exported .docx looks like** under a commit titled "portability". The census also deleted more than it kept: `BeginFigure` is FILLED at 7/7 sites and `EndFigure` CLOSED at 7/7, so both enums are gone, and 4/4 arc sites pass `{r,r}`, `0.0f` and CLOCKWISE, so three of five `ArcSegment` fields are gone — but *both* `AddLine` and `AddLines` stay, because a **shape** census rather than a name census found MSVC's array form at 10 of 26 sites. `App::LayoutShape::geometry` becomes a `unique_ptr` to the abstract type, read off the producers (all four declare their local inside a `case` block and `break`) rather than defaulted to, which deleted **both** remaining hand-written release loops. **17 new geometry assertions and 19 new pixel assertions, the latter running on both backends through the shared harness** — so "the two agree" is a measurement, not two self-consistent suites. Honest limits: this frees **no file** — `render.cpp` still needs four more seams — the full-circle divergence above is a **named, unfixed bug**, and `print.cpp` and `export_docx.cpp` are deliberately *not* routed, because they draw through raw device contexts no `tinta::RenderTarget` wraps |
| The segfault this seam caused, and why it is a row | Fixed — and worth recording because it passed every assertion before it crashed. The seam first cached its WIC and D2D factories in process-lifetime statics, on the reasonable-sounding argument that a factory should outlive one call. But the COM apartment here is entered and left **per call**, so the first call created a factory, used it, and then `CoUninitialize` tore down the apartment that factory belonged to; the second call found a non-null pointer to a dead object and the Windows binary died at exit 139. Linux was green at 56/56 the whole time, so **cross-platform agreement is not a defect detector** — both backends passed, and only one then died. The mechanism was isolated with a one-line diagnostic and a prediction written before the run, rather than fixed on a hypothesis, and the fix is not an invention: `git show` of the pass base proves the original code already created WIC *inside* the apartment and released it before closing. Fixing it surfaced a second latent bug from the same split — `fetchToTempFile` had silently lost its apartment, a failure indistinguishable from an unreachable host, which is exactly the one endpoint no test covers. The lesson that generalises: the crash needed the seam called **twice**, and a test calling each function once would have passed |
| Line-breaking seam (`TextFactory::AnalyzeLineBreaks`) + both backends | Done — **and this row is itself a correction: the pass that did the work added no row here.** It moved a 75-line `IDWriteTextAnalysisSource`/`Sink` COM pair out of `render.cpp` into `src/text_d2d.cpp` and wrote the Qt half over `QTextBoundaryFinder`, which is why the file's line-breaking mentions are now zero live. The interesting part is the **contract**, three clauses of which two are the kind a backend gets wrong while still compiling and still drawing plausible text, so they are spelled out in `include/text_api.h` rather than left to fall out: `canBreak[i]` means "may break *before* `text[i]`" in the **caller's** `wchar_t` units and must not be resized; an implementation writes indices `1 .. length-1` **only**, because Qt reports a boundary at both ends and the ends belong to the caller; and returning `false` means "this backend did not analyse the text", never "there are no break opportunities" — the caller then falls back to space-only breaking, which is exactly what it already did when DirectWrite's analyzer was absent, so no caller sees a new shape. Caller units are load-bearing on Linux, where `wchar_t` is UTF-32 and one caller unit can be two `QString` units, so `QtTextFactory` maps them rather than assuming they agree. Why it matters beyond tidiness: the question is about the **script**, not the font — Chinese prose has no spaces, so a backend that cannot answer treats a whole sentence as one unbreakable word |
| FILETIME seam (`platform::fileWriteTicks`) + both backends | Done — and it is the row where **a blocking comment in the tree turned out to be right about its premise and wrong about its conclusion.** `src/file_utils.cpp` carried a 23-line comment arguing that porting it meant a representation change across eleven sites, because `App::lastFileWriteTime`, `LayoutTab::lastWrite` and `settings.ini`'s `[Recent]` block all mean *Windows FILETIME ticks* — 100ns intervals since 1601 — and packing a POSIX timestamp into the same 64 bits satisfies every `CompareFileTime` equality in `input.cpp` while **redating the start page by 369 years**. That premise is correct and is preserved verbatim; two of its claims were also checked mechanically and **the comment was right where my own census was wrong** — `input.cpp` touches five file-time values and `tabs.cpp` one, where `rg FILETIME` finds three and zero, because a **type-name census cannot see a site that touches the value without naming the type**. Its *conclusion* was the error: packing is not the only portable move. The seam produces genuine 1601-epoch ticks from `std::chrono`, which `settings.cpp` had already been doing since before the comment was written, so nothing about what a file time *means* had to change. The comment was **corrected in place, not deleted.** 5 g++ errors to 0. The Windows half also fixes a real defect it inherited: the original ignored `GetFileTime`'s return value and passed the member's address straight in, so a failure left a half-written `FILETIME`; the seam returns 0 and the caller leaves the member alone, because `input.cpp` compares for *inequality* and a zeroed member would look like a change. POSIX reads `st_mtim` rather than `st_mtime` — whole-second resolution misses a same-second rewrite, which is exactly the case the polling exists for — and does the arithmetic signed then checks, because a pre-1970 mtime would otherwise wrap into a plausible far-future value |
| File-picker seam (`platform::requestOpenPath`/`requestSavePath`) + both backends | Done — all five `OPENFILENAMEW` sites routed, `commdlg.h` cleared from four files, and the seam exists in the shape it does because a **shape** census contradicted a name census. Five sites look like five copies of one call; exactly **one** of them reads `nFilterIndex` — `export_html.cpp`, where 2 means .docx and 3 means .pdf — so a seam returning just a path would have compiled at all five sites, passed every test in the tree, and **silently turned every DOCX and PDF export into an HTML file**. The index is therefore returned, and returned **1-based**, so that file's `== 2`/`== 3` comparisons are untouched. Flag divergence is *carried*, not tidied: `OFN_NOCHANGEDIR` at 3 of 5 sites and `OFN_HIDEREADONLY` at 1 of 5 become request fields, while the flags that are unanimous per direction — `OVERWRITEPROMPT`+`PATHMUSTEXIST` at 3/3 saves, `FILEMUSTEXIST`+`PATHMUSTEXIST` at 2/2 opens — live in the backend, because a seam preserves behaviour rather than improving it. The Qt half uses a `QFileDialog` **instance** rather than the static helpers, and that is a measured choice: `setDefaultSuffix` exists only on an instance, so reaching for `getSaveFileName` would have silently dropped the default extension at 2 of the 5 sites. **30 assertions, and the five that carry the weight compare the generated Windows filter buffer byte-for-byte against the literals the sites used to pass** — including embedded NULs, which is the one thing no debugger, logger or string printer will show you, since all three stop at the first one. A planted defect dropping the buffer's final NUL was predicted to fail exactly 7 assertions and failed exactly 7. Honest limits: **neither dialog function is asserted at all** — neither returns until a human clicks, so `file_dialog_tests` links the platform's backend to have it *compiled* and asserts none of it — and `buildQtNameFilter` currently **doubles the parenthetical** (`Markdown (*.md)` becomes `Markdown (*.md) (*.md *.markdown)`), which is pinned in a test rather than fixed, because unpicking it means either a stripping heuristic or changing what Windows displays, and neither belongs in a portability commit |
| HTML export portable and asserted (`export_html_tests`) | Done — **31 g++ errors to 0, and the first file in eleven passes to leave the to-do list rather than joining it.** It cost three things and no seam: the file-picker seam above took its `OPENFILENAMEW` block, `toUtf8` moved out of `editor.cpp` into `text_utils.cpp` (`export_html.cpp` needed it and cannot reach into the editor), and its two wide-path `fstream` constructions became named `const std::filesystem::path` locals — 2 of C58's 22, the MSVC extension libstdc++ rejects. **44 assertions against the real markdown parser**, not a hand-built tree: the fixture is a document with escaping traps in it — `# Title & <Subtitle>`, `` `a < b` ``, a code block containing `if (a < b && c > d) { return "<script>"; }`, a link whose href has `&` in it, and literal UTF-8 bytes for `café 日本語` — parsed by `parseDocument`, exported, and read back off disk. The escaping cases are the point: an exporter that forgets `&` still produces a file that opens, looks right at a glance, and mangles one entity per document. Six symbols are **stubbed, and every stub records that it was called** — `exportDocxFile`, `exportPdfFile`, `mathParse`, `mathBoxSvg`, `resolveDiagramRole`, `signalPushKey` — because a stub silently returning a default turns "this path is never reached" from a claim into an assumption; the test asserts all six tripwires stayed cold. Four planted defects, each with its failure count written in the script *before* the run, matched exactly (5/1/1/2). Honest limits: those six stubs mean the math and diagram branches and the whole of `exportDocumentAs` are untested, and `syntax.cpp` turned out to be **removable** from the link line — which is not tidiness, it means **HTML export emits code blocks unhighlighted** |
| The source list that linked first time and was not minimal | Fixed, and it is a row because the same lesson has now been learned twice. `export_html_tests` linked on the first attempt, ran green, and its CMake comment said "THE SOURCE LIST IS MINIMAL, MEASURED BY REMOVAL" — **written before the measurement, and false.** A successful link proves a list is *sufficient* and says nothing about whether it is *minimal*; five entries were unnecessary and everything was green. Worse, the first removal sweep found a **local** minimum: `i18n.cpp` read as "needed, 1 undefined symbol" only because `i18n_app.cpp`, one of the four being removed, referenced it. Minimality has to be re-measured after every removal, on a **clean** build directory, or stale objects answer for absent sources. Final reading: 21 objects, 0 removable, and the positive control — omitting `export_html.cpp` itself — fails loudly |
| The cross-CRT heap assertion on the Windows Qt tests | Fixed — **and it had been reporting itself as a pass.** Both Qt test targets inherited the tree-wide static CRT from `CMakeLists.txt:20-22` while `Qt6Cored.dll` uses the dynamic one, so a block allocated by one debug heap was freed by another and `__acrt_first_block == header` fired at `debug_heap.cpp:996`. Diagnosed with `dumpbin -dependents`, not by reasoning about it. The reason it survived is the part worth recording: a **modal** CRT assertion dialog makes `ctest` report one defect two opposite ways — dismissed with Ignore the process exits 0 and ctest says *Passed*, aborted it says *\*\*\*Failed*. `render_api`'s recorded "266 assertions pass on Windows" was obtained the first way. The **durations** were the tell: 63.98s and 28.27s before, 2.19s and 2.07s after — the old numbers were a dialog waiting for a click. Both targets now override `MSVC_RUNTIME_LIBRARY` to `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` (that spelling and not `MultiThreadedDLLDebug`, which CMake rejects — and the failed *generate* still let the build report exit 0 off the stale cache, so configure's exit code has to be read first), both import `ucrtbased.dll`, and the 266 was re-taken cleanly. **This overrides two TEST targets and deliberately does not pre-empt the product decision underneath it** — see the open question below |
| Qt entry point — a window that drives tinta's own seams (`main_qt.cpp`, `qt_window.cpp`, `qt_init.cpp`) | Done — **and it is the first pass in this port that cost nothing anywhere else: no file split, no seam cut, no call routed, no Windows half added.** Three new files, 189 lines, and every existing file untouched. `Seated<>::install()` went from **zero** callers to two, which means a template member that had never been instantiated is now the only way the app gets a backend — and the two lifetimes are asymmetric, which is the one design decision in it: `QtTextFactory` is stateless and installed for the process, while `QtRenderTarget` *begins* a `QPainter` in its constructor and ends it in its destructor, so its scope is exactly one paint event. Hence one install function plus an RAII `QtScopedRenderTarget`, not two install calls. **`tests/qt_entry_tests.cpp` is the first test in this tree to paint a tinta window** — 20 assertions, and it exercises the same `QtAppWindow` class the app runs, which is why the class lives in a header at all: `main_d2d.cpp` has the opposite shape and that is precisely why no test on either platform had ever painted a tinta window before. `QWidget::grab()` turned out to paint an **unshown** widget, measured rather than assumed, so the test needs no display and no event loop. The reading that carries the weight is a **count, not a boolean**: non-background pixels inside the text band, 3,698 on Linux and 2,880 on Windows, against a control run with the text factory released that reads **0**. Those two numbers are deliberately never compared to each other — the font substitution differs per platform, and a test asserting they match would be asserting the font database. Two planted defects, each with its failure count written down before the run, failed in exactly the predicted 2 and 1 places; plant 1 is the instructive one, because it proves two of the assertions read **green while nothing is drawn at all**, which is why the `painted > control` comparison exists and not just the two counts. Honest limits: **it draws no document** — see the twelve-cause list above — there are no menus, tabs, input handling, settings load or custom themes, each absent for a stated reason rather than forgotten, and **nobody has looked at the window**; `exit 124` under a timeout proves the process lives, not that a human saw anything |

**To do — 24 files, 28,478 lines**

**A file LEFT this set this pass, and it is the first one to leave since `file_utils.cpp`.**
25 → 24 files, 29,929 → 28,413 lines, −1,516. Two terms, derived from `git` across the whole
set rather than across the files I remembered touching: `src/export_html.cpp` is portable,
linked and *executed* off Windows, so its 1,515 lines leave the list entirely, and
`editor.cpp` lost 1 net when `toUtf8` moved out of it into `text_utils.cpp`. Nothing else in
the set moved by a line.

That is the shape the section below says is the *better* reading, and it is worth naming
what it cost, because the two halves of this pass pulled in opposite directions. The
file-picker seam came first and moved basis A **backwards** — 36.7% → 36.6%, a new
Windows-only file in this list against a Qt half in the manifest. Taking `export_html.cpp`
to zero errors and giving it a running test is what paid for both, and it is the only
kind of pass that can: a file crosses the boundary rather than a seam being added at it.

**The paragraph below describes the pass before this one, when a file *entered* the set.**
Both readings are kept, in order, because a reader watching only the headline sees a
seam-cutting pass as the port going backwards and the next pass as it lurching forward,
when it is one movement measured twice.

**A file ENTERED this set that pass, and that is the seam working rather than the port
regressing.** 24 → 25 files, 29,839 → 29,929 lines. Derived per file from `git`, across the
whole set rather than across the files I remembered touching: `file_dialog_win32.cpp` is 78 new
lines, `export_html.cpp` +15, `input.cpp` +1, `pandoc.cpp` +1, `main_d2d.cpp` −1,
`editor.cpp` −4 — +90 net.

**The first version of that sentence SUMMED TO +90 WITH TWO OF ITS TERMS WRONG**, and it is
recorded here rather than quietly fixed because the way it failed is the more useful half. It
said the new file was 94 lines (it is 78) and that every other file in the set was untouched
(`export_html.cpp` gained 15 and `pandoc.cpp` 1). The +16 overstatement and the +16 of omitted
growth cancelled *exactly*, so the total agreed with the tree, the arithmetic in the sentence
checked out on its own terms, and every figure around it was green. `check-readme-figures.sh`
could not have caught it either: it re-derives the **totals**, and a total is precisely what
this claim got right. A decomposition of a *delta* has no instrument here at all — after the
commit lands, the "before" state is a parent commit the script never reads — so the only
protection is deriving it from `git` at the moment of writing, which is what the figures above
now are.

This is the shape every seam has and the one the file-count bases cannot flatter: **cutting a
seam adds two `src/*.cpp` files and only one of them can ever be portable.** The Qt half joins
the manifest, the Windows half joins this list, and the denominator of every file-count basis
grows by two. It is the reason basis A fell 36.7% → 36.6% in a pass that made a Windows-only
file portable — and worth stating plainly, because a reader watching only the headline would
read a seam being cut as the port going backwards.

**29,834 → 29,839 the following pass, and the 5 lines are a comment.** No file entered or
left the set; `src/settings_win32.cpp` grew by five lines of correction, because it justified
staying Windows-only by pointing at `platform_paths_posix.cpp` as the precedent — and that
pass deleted the precedent. The paths seam collapsed to **one** non-Windows file instead of
splitting into two, so `settings_win32.cpp` is now the weaker case rather than the matching
one, and it says so where the next reader will look. Five lines on the to-do total is what
that costs.

**That "29,959" is a correction, not a restatement, and it falsifies a claim this section
used to make about itself.** The header here read "25 files, 29,903 lines", and 29,903 is the
total at `cc292d4` — one pass earlier. The line-breaking pass changed three of these files
and did not re-derive the total, so it was stale by 56 lines *in the reassuring direction*,
understating the work remaining. Two passes ago this section noted that three hand-typed row
figures had rotted while "the **total** was right", and concluded "where a number can be
derived, derive it." The conclusion was correct and the premise was already false: the total
is not derived by anything, it is typed like the rows, and it rotted the very next pass. The
only difference is that a wrong total has no neighbouring figure to disagree with, so nothing
catches it short of re-deriving it — which is now done every pass, both directions, against
the manifest the probe checks.

The direction of travel is still expected to be *upward* on some passes, and that is not a
failure: **every seam's Qt half is progress and its Windows half is a larger deletion
later**, so lines accumulate in the backends before they leave in one piece. The pass before
last added 141 net, 116 of them `render_d2d.cpp`'s `D2DPathSink`. A falling total is the
better reading but a rising one is not the surprising one.

| Area | Size | Note |
|---|---|---|
| `render.cpp` | 3,318 | **Every seam this file needed has now been cut, and what is left is two deliberate exclusions plus one type name.** Three *unguarded* Windows headers — `urlmon.h`, `shlwapi.h`, `d2d1_3.h` — used to stop `g++` at the first one, reporting `errors=1, fatal=1`, which means not-measured rather than one-error-away. The image seam took all three, and the file then read **`fatal=0`, `errors=38`** — 97 → 70 → 47 → 38 across the four passes after that, and read with `fatal` first and `g++`'s exit status before either, because that is the only thing making the number a reading at all. **THAT 38 IS CARRIED, NOT RE-TAKEN, AND THIS ROW NO LONGER CLAIMS IT FOR THE CURRENT TREE.** The instrument behind it was a set of 21 hand-written stub headers built in a scratch directory and never committed; it is not on this machine any more, and a census is only comparable when the instrument is byte-identical, so re-deriving one approximately would produce a delta that mixes this pass with a change in the rig. What *is* re-derived below is the mention count, which needs no compiler: **27 total, 10 live**, down from 38 / 22. **Path geometry is now zero live** — the three remaining `ID2D1PathGeometry`/`ID2D1GeometrySink`/`D2D1_ARC_SEGMENT` mentions in the file are all inside comments explaining what the seam replaced. So is `dwriteFactory`: every layout goes through `app.textFactory`, and the single surviving `CreateTextLayout` string is likewise a comment. **Line breaking is now zero live too**, moved behind the `TextFactory` seam with a `QTextBoundaryFinder` backend. An earlier version of this row claimed "all 18 `createLayout` sites still call `dwriteFactory->CreateTextLayout` directly" and **that was false when it was written** — the `18` was a `grep` count of a string, not of call sites, and the text path was already on the seam. The same false sentence also sat in the completeness section below and survived there three passes longer, which is the argument for correcting a figure in *every* place it appears rather than the place you noticed it. **What remains is now exactly two things, and both are documented refusals rather than unfinished work.** All 10 live mentions fall inside them: the WIC decode for the local-raster path (`:2535-2570`, 7 mentions — routing it through the image seam would add a full-image memcpy to every local image, and `include/image_api.h` carries the argument) and the `IDWriteTextLayout2` font-fallback query (`:79-82`, 3 mentions), which is deliberately *not* pushed into the backend — see the note below the table. Beyond the D2D/DWrite/WIC pattern the file has one Win32 identifier left, the `HWND` parameter of `asyncImageWorker` at `:2412`. **Two clauses that used to sit here are gone and one of them was WRONG WHEN WRITTEN:** Win32 messaging (`PostMessageW`, `SetTimer`) now routes through `requestRepaint` and `win32_api.h` and greps to zero, and bitmap creation went behind `RenderTarget::CreateBitmap` — but that clause named `ID2D1HwndRenderTarget` as one of the two things to move, and `ID2D1HwndRenderTarget` **appears nowhere in `src/render.cpp`** and appears not to have when the sentence was written either. It lives in `include/app.h` and `include/render_d2d.h`. The wording had been carried forward from a backlog note about a different file, which is the failure mode this README keeps warning about in the other direction: a stale figure looks wrong eventually, a stale *identifier* reads as a considered decision forever. The last cut, from 47, was the cheapest kind and worth naming as such: two `UINT` members respelled `unsigned`, seven of the nine errors having been cascade from them. Honest limit, unchanged and important: **no test links `render.cpp`**, so all of these are compiler readings |
| Window and message loop | 4,349 + 2,276 | `input.cpp`, `main_d2d.cpp` — Win32 message pump → Qt event handling. Both lost `commdlg.h` this pass: `input.cpp` routes its pandoc-locate dialog through the file-picker seam, and `main_d2d.cpp`'s include was simply **dead** — no `commdlg` API appears anywhere in the file |
| Editor | 3,133 | `editor.cpp`, including IME input via `imm.h` → Qt input methods. `commdlg.h` is gone; `imm.h` at `:2439-2471` is what remains. One line lighter this pass, because `toUtf8` moved out of it into `text_utils.cpp` — `export_html.cpp` needed it and could not reach into the editor for it |
| Overlays, annotations, math | 4,108 + 1,194 + 852 | No Windows headers left; blocked on the layout seam above and on function-local Direct2D construction. `tableedit.cpp` used to be the fourth entry here and has left — it compiles off Windows now |
| Exports and printing | 1,563 + 1,018 + 229 | **Three files, not four: `export_html.cpp` has LEFT this row** — it is portable, linked and executed off Windows, and it is in the manifest. `scripts/check-readme-figures.sh` was edited in the same pass so it no longer derives this row from that file, and the comment there says why, because this is the one row that can rot in the *other* direction: a checker that keeps deriving from a freed file asserts a figure that must no longer include it. **`commdlg.h` → `QFileDialog` is DONE for the file dialogs** — all five `OPENFILENAMEW` sites route through `include/file_dialog.h`, and the header is now the only file-dialog `commdlg.h` include under `src/`. `print.cpp` keeps its own for `PRINTDLGW` at `:580`/`:603`, which is a different dialog and a seam nobody has cut. Printing → `QPrinter` is still entirely ahead. They grew rather than shrinking, and deliberately: `export_docx.cpp` and `print.cpp` are the two callers the path geometry seam **did not** route, because they draw through raw device contexts that no `tinta::RenderTarget` wraps, and each now says so at the site. `export_docx.cpp` also carries the write-up of the full-circle slice divergence it is on the wrong side of |
| Shell integration | 1,719 + 836 + 160 | `shellapi.h` → `QDesktopServices` |
| Clipboard, misc | 198 | `utils.cpp` → `QClipboard` and `std::filesystem`. **`file_utils.cpp`'s 158 lines left this row this pass** — it was the "paths" half, and it is now portable *and* executed on both platforms |
| Startpage | 792 | `dwrite_1.h`; also uses `SetCharacterSpacing`, which stays native |
| Device and factory creation | 689 | `d2d_init.cpp` — `objbase.h`, COM initialisation and the Direct2D device; becomes Qt paint-device setup |
| Modifier-key sites still native | — | The seam is built (see Done) and took 2 of the 17 `GetKeyState` sites. The other 15 — 7 in `editor.cpp`, 8 in `input.cpp` — sit behind fatal `imm.h` and `windowsx.h` includes, so they cannot be *measured* yet, let alone rewired: those files report `errors=1, fatal=1`, which means not-measured rather than one-error-away |
| Qt entry point | — | **Built. This row used to read "`Seated::install()` still has zero callers; blocked behind `render.cpp`", and half of that was wrong: the entry point was blocked behind nothing.** It needed a *measurement* — which objects a Qt `main()` actually pulls — and the measurement came back as nine files rather than the whole tree, because `App` reaches its backends through pointers now. What is still blocked behind `render.cpp` is the **document**, not the window. See the twelve-cause list above |
| macOS | — | Not attempted. Nothing platform-specific to macOS has been written or measured |
| The Windows backends themselves | 375 + 767 + 217 + 173 + 65 + 23 + 346 | `render_d2d.cpp`, `text_d2d.cpp`, `settings_win32.cpp`, `platform_paths_win32.cpp`, `settings_app.cpp`, `keyboard_win32.cpp`, `image_win32.cpp` — **deleted or replaced, not ported.** 7 files, 1,966 lines, re-derived rather than carried (`text_d2d.cpp` read 635 for a pass after gaining 132 lines of line-breaking backend, and `platform_paths_win32.cpp` read 146 after gaining the FILETIME seam's 27). Registry access becomes `QSettings`, path lookup becomes `QStandardPaths`, and the backends go once Qt covers what they do. `image_win32.cpp` is the newest and the largest single addition to this row: 346 lines, of which a good share is the post-mortem for a segfault it caused (see below). `keyboard_win32.cpp`'s 23 lines make the same point more cheaply — every seam's Qt half is progress and its Windows half is more lines to delete later |

The font-fallback query is the one place where the obvious tidy-up is the wrong move, and
it is worth a note because the reasoning is not visible from the code. Folding
`SetFontFallback` into the backend's `CreateTextLayout` would make it invisible to every
caller — and would change behaviour in files nobody touched. Fifteen sites already create
layouts through `app.textFactory` and none has ever had a fallback, which
`git log -S'SetFontFallback'` confirms across the whole history; several of them *measure*
text, so giving them a fallback would move measured widths and therefore layout. It stays
in `render.cpp` as one named helper, which also makes it the single line to delete if that
answer ever changes.

That last row is the change of goal made arithmetic. These files used to be listed
separately as "will never move", excluded from the count on the grounds that they were
the Windows half of a permanent pair. Under one stack there is no permanent Windows half,
so they are work — **1,966 lines of it today**, across seven files: the five originally
excluded, plus the two seam backends written since — and they are counted.

The *membership* of the to-do set is derived mechanically — every `src/*.cpp` that
`tests/portable-sources.txt` does not list — so it cannot drift out of step with the manifest
the probe checks. Its **line figures are not**, neither the rows nor the total, and every
attempt this file has made to claim otherwise has been wrong within one pass:

- Three rows were stale by a combined 50 lines two passes ago (`math_render.cpp` 848→852,
  `d2d_init.cpp` 674→681, `text_d2d.cpp` 596→635), noticed then and fixed then.
- The **total** was stale by 56 lines this pass, having been declared reliable in the same
  paragraph that fixed those rows.
- Two more rows were stale by a combined 159 lines this pass (`text_d2d.cpp` 635→767,
  `platform_paths_win32.cpp` 146→173), and `d2d_init.cpp` had rotted again, 681→683.

The pattern is not "hand-typed figures rot and derived ones do not". It is that **nothing
here is derived at read time**, so the only thing separating a good figure from a bad one is
whether somebody re-ran `wc -l` this pass. Which is why they now all get re-run, and why the
reconciliations are written down: a total that reconciles against its own deltas is checkable
by a reader, and a bare total is not.

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

> Every countable figure in this section is now re-derivable by `bash
> scripts/check-readme-figures.sh`, which reads the tree and asserts that the README says what
> the tree says. It exists because five figures here were found stale in one pass, each of them
> *correct when written*, and because nothing else in the repo can read prose: the probe checks
> the manifest, `ctest` checks the tests, `g++` checks the code. Run it with `--control` and
> every claim must fail; a green run that has not been controlled proves only that `grep` found
> something.

| Basis | Measured | |
|---|---|---|
| A. Source lines free of the Windows stack | 23,470 / 59,934 | **39.2%** |
| B. `src/*.cpp` files compiling without Windows headers | 42 / 66 | 63.6% |
| C. Files a *running* test links | 38 / 66 | 57.6% |
| D. Test targets that *could* move, and do | 27 / 33 | 81.8% |
| E. All test targets, run off Windows | 27 / 37 | 73.0% |
| F. **Platforms running Tinta on the target stack** | 0 / 3 | **0%** |
| G. Platforms where the Qt frontend builds, links, starts and paints | 2 / 3 | 66.7% |

**Basis F did NOT move, and that refutes what I said before building the entry point.** The
plan for this pass called it "the only work that can move basis F off 0/3". It was not: F says
*running Tinta*, and Tinta is a markdown viewer. A window that opens, clears to the theme
background and centres one line of placeholder text is not the product on a new stack, and
scoring it as one would make the headline basis of this whole document mean "a window exists".
So F stays at zero and basis **G** is added underneath it for what actually moved — the move
this file has already made twice, both times for the same reason: basis A's ceiling is stated
separately because the metric cannot reach 100%, and basis C keeps a *linked* versus *called*
judgment because crediting a link that executes nothing would make it a second way to count B.
Splitting a metric that cannot register the work beats bending one that can't. G is 2/3 rather than 3/3 because macOS remains
unattempted: nothing platform-specific to it has been written, built or measured, and Qt being
cross-platform is an argument, not a reading. **The distance between G and F is exactly the
twelve-cause list**, which is the useful thing about having both rows.

**Basis A moved on new files rather than on ported ones, which is a first, and the bookkeeping
is worth stating because it cuts both ways.** Numerator 23,281 → 23,470 (+189) and denominator
59,604 → 59,934 (+330); the 189 is `qt_init.cpp` 40, `qt_window.cpp` 86 and `main_qt.cpp` 63,
and the remaining 141 is `qt_init.h` 77 plus `qt_window.h` 64. Every other file in the tree is
byte-identical, so those two sums are the whole delta and they reconcile against the tree
exactly. The headline reads 39.1% → 39.2%, and it is the first rise in this port that came from
code **written for Qt** rather than from a Windows file crossing the boundary. The cost side:
the two new headers are denominator-only forever, so basis A's own ceiling *fell*, 86.8% →
86.7%. Writing a class into a header so a test can construct it is what did that, and it was
the right trade — it is the reason a test paints a window at all.

**Basis C stopped being hand-kept this pass, and the check is the reconciliation rather than
the number.** Every previous pass typed that numerator from memory. It is now measured — a
full Linux build, `ctest -N` for the set of tests that actually run, then the union of
`src/*.cpp` objects across those targets' link lines — and at that pass's tree the mechanical
union read **37** against a stated 36, one higher. The difference is `file_dialog_qt.cpp`, linked
by a running
test and called by none, which is exactly the file the judgment below subtracts. So the
instrument agrees with the standing judgment instead of replacing it, and re-deriving at the
previous tree state returns 35, which is what the hand-kept figure said. That reconciliation
is the reason to trust the figure at all; the union on its own would have been a second way to
count basis B. The same gap of exactly one persists at today's 38, for the same one file.

**Basis C's numerator did not move, and that is a judgment stated rather than an oversight.**
`file_dialog_qt.cpp` and `file_dialog_win32.cpp` are both *linked into* a running test —
`file_dialog_tests` compiles the platform's half so the manifest can honestly list the Qt one —
but neither is ever *called*, because `requestOpenPath` and `requestSavePath` do not return until
a human clicks a dialog. Crediting a link that executes no instruction would make this basis
measure the same thing as B while sounding like coverage, so the numerator was unchanged that
pass and only the denominator moved. **The pass after it moved 35 → 36, on `export_html.cpp` —
which is the distinction working rather than being abandoned: that file is not merely linked,
its `exportHtmlFile` is called 44 times by assertions.** **This pass it moved again, 36 → 38,
and both additions clear the same bar: `qt_init.cpp` and `qt_window.cpp` are not merely linked
into `qt_entry_tests`, their `installQtBackends` and `QtAppWindow::paintEvent` are what the 20
assertions execute. `main_qt.cpp` is deliberately not counted — it holds `main()` and no test
links it — so the mechanical union now reads 39 against this 38, the extra still being
`file_dialog_qt.cpp`.** Bases D and E moved by one target
each again: Windows now configures **37** targets and Linux **27** (28 ctest *entries*,
because `platform_paths_tests` is registered twice with different environments — the count
that moves is entries, not targets), and D still excludes the four targets that are
Windows-only by subject. The Linux set is a strict subset of the Windows set, measured by
diffing the two `ctest -N` listings rather than assumed: the only entry Linux has and Windows
does not is that second entry of a target Windows already runs.

**Basis A, 39.2%, is the headline** — it is the figure the probe itself prints on every
run, so it is the one comparable across time. It *understates* progress, because its
denominator includes headers the manifest never credits even when they are portable. And
it *overstates* readiness, for a reason this pass demonstrated about as sharply as it can
be demonstrated.

The understatement is now measured rather than asserted. The probe's numerator sums only
the `src/*.cpp` files the manifest lists, while its denominator counts every `.cpp` *and
`.h`* under `src/` and `include/` — so a header line is in the denominator permanently and
can never enter the numerator. With `src/*.cpp` totalling 51,948 lines of the 59,934,
**basis A's ceiling is 86.7%, not 100%**, and 39.2% should be read against that.

That has a small edge worth admitting, because the image-seam pass walked into it too. The
denominator moved 57,997 → 58,026 *during* that pass: 29 lines, all of them
`src/image_win32.cpp` growing when the post-mortem for a segfault went in as a comment
above the class that fixed it. Writing down why the obvious design was wrong costs a
fraction of a point on the headline, and it is worth it. It is the same honest bookkeeping
as `keyboard_win32.cpp`'s 23 lines being counted as work still to delete — and it is the
one figure in this section that the checker could not catch on its own, because the probe's
output is passed to it as a witnessed reading rather than re-derived. The 29 lines are also
why an independent line count disagreed with the probe by exactly 29 until the probe was
re-run at the final tree state; two instruments agreeing at 58,026 is the reading, and one
of them being stale was the reason to re-run rather than reconcile on paper.

**This pass is the extreme case of that, and it is worth reading before the two below it.
295 call sites moved behind four seams — every raw `InvalidateRect`, `SetTimer`, `KillTimer`
and `PostMessage*` under `src/` is gone — and basis A's numerator did not move by one line:
21,620 → 21,620.** The denominator moved 58,914 → 58,936, and all 22 of those lines are
comments I wrote correcting two headers whose prose had gone stale, so the only measurable
effect of the largest single-site change in the port so far is a headline that rounds to the
same 36.7% and, unrounded, is *fractionally worse* (36.696% → 36.684%).

The reason is the one already stated — the metric prices set membership — but this pass
sharpens it into something more useful: **all 295 sites live in files that remain
non-portable, so no site can possibly cross a set boundary, and basis A is structurally
incapable of registering the work.** A different instrument saw it clearly. The portable
`-fsyntax-only` census fell **2,711 → 2,479 errors, −232**, measured as a paired before/after
run over the same tree at two commits with byte-identical flags, so untouched files acted as
their own control and every one of them reported a delta of exactly zero.

The gap between 295 sites and 232 errors is not slippage, and it was chased down rather than
waved at. Per file, the drop equals **exactly** the number of routed sites that were emitting
a diagnostic before — 4 of 41 in `main_d2d.cpp`, 48 of 69 in `editor.cpp`, 139 of 143 in
`input.cpp`, 0 of 1 in `pandoc.cpp`, 23 of 23 in `tabs.cpp`, five files out of five in
agreement. The other 63 sites were never diagnosed at all: g++ abandons the rest of a
function body after an earlier error, so in an error-dense file a bad call is invisible to the
count. **An error count is therefore a saturating measure — it can under-report a fix but
never over-report one** — and the first hypothesis for the gap, that those sites sat inside
`#ifdef _WIN32`, was refuted by preprocessing the files and finding all 295 still present.

*The pass before this one:* **the headline did not move either — 36.7% → 36.7% — while the
numerator FELL, 21,635 → 21,620, and every other basis stood still.** No file entered or left any set: one file in the
numerator was replaced by a smaller file in the numerator, 334 lines of `platform_paths_posix.cpp`
out and 319 of `platform_paths_qt.cpp` in, −15 on both numerator and denominator, and the
percentage rounds to the same place. So the pass that **deleted an entire hand-written POSIX
implementation** — a UTF-8 codec, a double `fork`, an `execlp`, a `waitpid` — is invisible in
the headline, and worse than invisible in the numerator, which reads it as a small loss.

That is the previous pass's lesson seen from the other side, and the two together say what
basis A is. Last pass it jumped 196 lines on 38 lines of new work, because a file crossed into
the set. This pass it fell 15 while the port got structurally simpler, because no file crossed
and the one that changed got shorter. **The metric prices set membership, and deleting code is
the one form of progress it can only read as shrinkage.** A seam that collapses two
implementations into one will always look like this, and there are four more seams to collapse.

The other bases held for a reason worth stating rather than assuming: B, C, D and E all count
*files* or *targets*, and this pass changed neither. `tests/portable-sources.txt` still lists 37
files, because line 43 changed its spelling and not the count. The one real change is invisible
to every basis in the table — the Linux suite went from 24 configured ctest entries to 25,
because `platform_paths_tests` is now registered **twice**, with different environments. That is
one target, so basis E stays 24 / 34, and a future reader tempted to bump it to 25 should not:
the count that moved is entries, not targets.

The reason for the second entry is the sharpest single reading of the pass. `preferredUiLanguages`
filters out the C/POSIX locale, and Qt resolves the system locale **once per process** and caches
it — so a test that forces `LANGUAGE` to real languages can never afterwards observe the filter
working, and one that does not force it observes nothing at all, because with no `LANGUAGE` the
list is `{"C"}`, the filter empties it, and every assertion in the loop runs zero times while the
suite prints green. Deleting the filter from the backend left the single-process test **passing**.
It now fails in the second entry, which ctest runs under `LC_ALL=C LANG=C LANGUAGE=` — measured
to yield exactly one tag — and asserts the result is empty: 2 failures of 50 with the filter gone,
0 of 45 with it there, while the first entry stays green through both. Two processes, because one
process physically cannot hold both readings.

An earlier attempt at that control is why this is stated so carefully. I had seeded the forced
list as `"C:zh_CN.UTF-8:ja_JP@x"` and written in the test that the leading `"C"` exercised the
filter. **Qt drops the leading `"C"` itself while resolving the list**, so it never reached the
filter, and the comment claiming otherwise sat in the file describing a control that could not
fire. The comment is corrected in place, and the claim it made is now carried by a test that
was proven to fail.

*And the one before that, for comparison:* the headline moved 36.5% → 36.7%, 21,439 / 58,809 →
21,635 / 58,924, and B, C, D and E each moved by one. A `.cpp` file crossed into the portable set, which is
the only thing that moves basis A, and the arithmetic of *how* it moved inverts the lesson of
the previous pass. The numerator gained **196 lines. Only 10 of them were written this pass.**

| Where basis A's +196 came from | Net | Creditable? |
|---|---|---|
| `src/file_utils.cpp` **crosses into the manifest** — 168 lines, of which +10 are new | **+168** | yes, the whole file |
| `src/platform_paths_posix.cpp` — the POSIX half of the FILETIME seam | **+26** | yes, already listed |
| `src/settings.cpp` — de-duplicating the 1601-epoch constant | **+2** | yes, already listed |
| `src/platform_paths_win32.cpp` +27, `src/render.cpp` +6 | **+33** | never — not in the manifest |
| `include/` — `platform_paths.h` +35, `win32_types.h` +9 | **+44** | never — header lines are denominator-only forever |

Read the first row against the last two. **Basis A is a set-membership measure, not a
line-production measure.** 158 of `file_utils.cpp`'s 168 lines had been sitting in the
denominator since the port began, earning nothing, and they all landed in the numerator at
once when the file's last Windows call went behind a seam. So a pass that wrote 38 creditable
lines moved the numerator by 196 — where the previous pass wrote 41 creditable lines and moved
it by 41, because it added no file. The corollary is the uncomfortable one: **the headline
rewards finishing a file and is nearly indifferent to how much work the finishing took.**
`file_utils.cpp` needed 5 errors cleared. `render.cpp` needs 38, is 20 times the size, and
crossing it would move basis A by roughly 5.6 points in one commit.

The ceiling fell again, 87.4% → 87.3%, which is the 44 header lines and the 27 in
`platform_paths_win32.cpp` showing up in the only figure that reads them honestly. Both
instruments were re-read at the final tree state — the probe's own denominator and an
independent `git diff --numstat` sum agree at 58,924 and at +115 net — which is the check,
rather than reconciling on paper.

**Basis D was redefined this pass rather than updated, because the old numbers could not be
derived.** It read "Windows tests that also run off Windows — 22 / 32", and nothing in the
tree recorded what the 32 excluded; measuring it found 34 targets on Windows, 24 on Linux and
**zero Linux-only targets**, under which the old wording is just basis E with two different
numerals. That is the same defect this section already flags for native-mention counts, so D
now states its own exclusion: of the 34 targets, four are Windows-only **by subject** and each
says so in `CMakeLists.txt` — `bitmap_seam` and `text_factory` and `render_d2d` assert on the
Direct2D/DirectWrite wrappers themselves and are gated on `TINTA_LEGACY_D2D` to be *deleted*
with the backends, and `config_paths` checks MSIX filesystem virtualization, which has no
non-Windows meaning. Excluding those four, 24 of 30 movable targets have moved.

**The loudest reading of the Qt paths pass came from the full suite, not from the target under
test, and it is the one worth carrying forward.** `platform_paths_tests` was built and green —
78 assertions, both environments, every planted control firing at its predicted count — while
`ctest` reported **15 of 25 entries Not Run**. Sixteen other targets also list the paths source,
and all sixteen failed at `QByteArray: No such file or directory`, because the file this seam
replaced needed no include path at all and the one target I edited by hand was the only one that
linked Qt. The seam's libraries were not travelling with the seam.

Two things in that are worth keeping. The dependency is now stated **once**, beside the file
selection, as `PLATFORM_PATHS_LIBS` — which also absorbed twelve hand-written
`if(WIN32) target_link_libraries(x PRIVATE shell32)` blocks, since `shell32` was only ever there
for `SHGetFolderPathW` in the same seam. And **building one target is not building the tree**: a
green target next to a suite that will not configure is exactly the shape a probe is supposed to
catch, and this one was caught by running everything rather than by reasoning about it. The
ceiling reading for the pass, for completeness: 51,469 / 58,924 → 51,459 / 58,914, still 87.3%.

The other readings of the pass before this one, each with its instrument named. `src/file_utils.cpp` went from
**5 off-Windows compile errors to 0** and `src/render.cpp` from **47 to 38** — the 9 being two
`UINT` members respelled `unsigned`, seven of the nine having been cascade. A new seam function
`platform::fileWriteTicks` has an implementation per family and its own assertions. The new
`file_utils_tests` target reports **37 assertions on Linux and 36 on Windows**, the 36 written
into the probe script as "37 minus the POSIX dot-file assertion" *before* the run rather than
read off it. `platform_paths_tests` reports 48 and 57, a nine-assertion divergence that was
invisible until this pass added the count to its success path — it had printed a bare
"passed" line for several spans, which is indistinguishable from a run whose `#ifdef`'d blocks
were never compiled. Five positive controls were run with the failing count predicted in the
script before each, every one exact, every restore `diff -q` byte-identical.

One of those five is worth naming, because it is the only reason the epoch is trustworthy.
Packing a POSIX timestamp into a `FILETIME`'s two halves satisfies **every** comparison the
tree performs on file times and misdates the start page by 369 years. Planting exactly that
defect fires in `platform_paths_tests` and is **completely invisible to `file_utils_tests`** —
0 failures — because a caller's test compares the caller against the seam, and both move
together. Only the seam's own test, which computes 1601-epoch ticks from `std::chrono`
independently, can see it.

This is the seventh consecutive pass where the headline was the wrong place to look, and the
seventh is the clearest case yet: it stood still to one decimal place while a whole
implementation was deleted. The pass before it moved in the right direction for the right
reason, which had not happened in five. Two passes before that it stood still
while the port gained its first tested image seam, then it fell while the port gained its
first portable path geometry, and before that it jumped a full point on `tableedit.cpp`, a
file nothing executes. Before that, giving `search.cpp` a
running test with 53 assertions moved basis A by **exactly zero** (the file was already
counted as portable), and `i18n_app.cpp` then added 42 assertions for another zero. Before
*that* it moved the wrong way outright, 35.8% → 35.7%, on a commit where `render.cpp` lost
40 live DirectWrite text mentions and gained a working seam — real progress that credits
nothing until the whole file compiles, while the comments explaining it enlarged the
denominator. A metric that can fall on a good commit and jump on an untested one is worth
keeping, because it is honest about what it measures. It is just not the metric to read for
whether the port is progressing. The `render.cpp` row and basis C are.

A basis was **removed** rather than updated when the goal changed. There used to be a
line reading "same, excluding the five never-move files — 36.6%", and it measured
something that no longer exists: under one stack nothing is excluded, because the Windows
backends are work rather than furniture. A percentage whose denominator quietly stops
meaning anything is worse than no percentage, so it is gone rather than adjusted.

Basis F is the one to keep in view, and the change of goal made it stricter: it used to
ask whether a non-Windows binary existed, and now asks how many of the three platforms
run Tinta on Qt. **The answer is none.** Windows runs, on the stack being replaced.
`render.cpp` is the single file standing between "the library compiles and is tested" and
"it runs anywhere on Qt", and it is the same blocker under either goal. **It is now 3,307
lines, and for the first time it can be measured off Windows at all.** (This figure read
"3,385" until now, and **3,385 was correct when it was written** — that is `wc -l` at
`092afcd`, the image-seam pass. Two passes then cut the file, to 3,379 and to 3,301, and
neither updated the sentence. A figure that was true when written is the harder kind of
stale: nothing about it looks wrong, and re-deriving it is the only thing that catches it.)
For six passes it
had three *unguarded* Windows headers in it — `urlmon.h`, `shlwapi.h` and `d2d1_3.h` — so
`g++` stopped at the first one and reported `errors=1, fatal=1`. That reading means NOT
MEASURED, never "one header away". The image seam took all three, and since then the file has
had an actual size: **97 → 70 → 47 → `fatal=0, errors=38`**, measured at each pass rather
than recalled, with `g++`'s own exit status read *before* the count so that a `0` from a
compile that never started could not be mistaken for progress. The first error is now an
ordinary undeclared type (`IDWriteTextLayout2` at `:81`, the deliberately-kept version
negotiation) rather than a wall.

Those 38 are **not** the cheap kind, and the pass that cleared `file_utils.cpp` is the
evidence: the 9 errors it took out of `render.cpp` were two members' spelling. What remains is
the WIC decode block, `D2D1_BITMAP_PROPERTIES` and `ID2D1HwndRenderTarget`, `PostMessageW` and
`SetTimer`, and that version-negotiating QI — each one a seam that does not exist yet rather
than a type that needs renaming.

Native-mention counts here are quoted **with the pattern that produced them**, because this
section previously carried "93 mentions, 75 of them live" from a measurement whose pattern
was never recorded, and a separate pass measured "128" under a different definition. Two
numbers with no definitions is worse than one number with one. Counting occurrences of
`\b(ID2D1|IDWrite|IWIC|D2D1_|DWRITE_|WIC[A-Z])[A-Za-z0-9_]*` plus `D2D1::`, and re-derived at
every commit rather than carried forward, `render.cpp` reads:

| Pass | `render.cpp` lines | Mentions | Excluding whole-line comments |
|---|---|---|---|
| `4e950a4` text path onto the seam | 3,498 | 100 | 85 |
| `092afcd` image seam | 3,385 | 75 | 60 |
| `cc292d4` path geometry seam | 3,379 | 55 | 38 |
| `d1351e8` line-breaking seam | 3,301 | 38 | 22 |
| current tree | 3,318 | 27 | 10 |

Note the trap, which this table exists to defuse: the same numerals recur at different
meanings. "75" is a total at one pass and a *live* count at an older pass under a different
pattern; "38" is simultaneously the current mention total, the live count two passes ago, and
`render.cpp`'s off-Windows error count. Read the column, not the digit. "Live" excludes
whole-line `//` comments only; it does not strip trailing or block comments, and saying so is
cheaper than implying a precision it does not have.

It is a *smaller* blocker than it was: text layout came off DirectWrite entirely, image
loading came off Windows, geometry and line breaking followed, and what is left is drawing
and Win32 messaging rather than typesetting or I/O. **The `createLayout` claim that stood here
was not merely stale — it was false when it was written, and it survived here three passes
after being corrected in the table above.** It read "all 18 `createLayout` sites still call
`dwriteFactory->CreateTextLayout` directly, so not one paragraph — not even plain text — can be
laid out off Windows yet." `4e950a4` had already routed them, two passes before the sentence
was last touched: `render.cpp` holds **one** `CreateTextLayout` call, at `:104`, and it goes
through `app.textFactory` — the seam — not through `dwriteFactory`. The `18` was a `grep` for a
string rather than a census of call sites; `render.cpp:90`'s own comment records the real
figure as 12, measured before the change. **The lesson is the duplication, not the number:**
the correct reading and the false one coexisted in this file for three passes because the fix
went in where the error was noticed instead of everywhere the claim appeared. **20 direct `dwriteFactory->CreateTextLayout` calls remain in the tree**, spread
over nine files — `overlays.cpp` 6, `editor.cpp` 4, `annotations.cpp` 3, `signals.cpp` 2, and
one each in `d2d_init.cpp`, `export_docx.cpp`, `math_render.cpp`, `startpage.cpp` and
`utils.cpp` — each behind its own blocker rather than behind this one. `export_html.cpp` left
this list when its diagram `measure` lambda moved onto the text seam, which was a *mirror* of a
port already in the same file: `wrapLines` at `:165` had been on the seam for spans, 750 lines
above the lambda still calling DirectWrite directly. A
`grep` for the call returns 21; the twenty-first is the comment in `render.cpp:90` recording
the census, which is exactly the kind of match that inflates a count nobody re-derives.

Call it roughly a third of the way through by volume, with the hardest single file still
ahead — and note that the finish line moved out, not in: **1,966 lines of Windows backend are
now work rather than furniture**, they grow with every seam before they leave in one piece,
and re-verifying Windows on a new stack is effort that appears in no line count here.

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

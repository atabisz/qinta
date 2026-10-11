<div align="center">
  <img src="resources/qinta.ico" width="80">
  <h1>Qinta</h1>
  <p><em>Markdown, distilled — on every desktop.</em></p>
  <p>A multiplatform fork of <a href="https://github.com/oipoistar/tinta">Tinta</a>, moving the whole stack to Qt 6</p>

  
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT License">
  </a>
</div>

<br>

**Qinta is a fork of [Tinta](https://github.com/oipoistar/tinta) that aims to run on Windows,
macOS and Linux from one codebase and one stack.**

Tinta is a fast, lightweight Markdown and Mermaid viewer for Windows — a single native
executable of about 2.3 MB that opens instantly, with no Electron, no web engine and no
installer. It gets that speed from the GPU-accelerated text stack built into Windows: Direct2D
for drawing, DirectWrite for text, Win32 for windowing. Which is also exactly why it runs only
on Windows.

Qinta keeps the viewer and replaces the stack underneath it. The rule the fork is built around
is that there is **one stack on all three platforms, and it is Qt 6** — not Direct2D on Windows
with something else elsewhere, because two stacks means every feature gets designed twice and
diverges quietly. [Cross-platform port](docs/port.md) has the
goal, why Qt was chosen over Skia, how the port is structured, and a status table that counts
rather than estimates.

<p align="center">
  <img src="https://tinta.cc/img/screenshots/paper.png" width="49%" alt="Tinta markdown viewer on Windows — Paper light theme">
  <img src="https://tinta.cc/img/screenshots/midnight.png" width="49%" alt="Tinta markdown viewer on Windows — Midnight dark theme">
</p>

<sub>Those two screenshots are upstream Tinta's, on Windows, via the Direct2D stack. Qinta's own
witnessed screenshots are in [docs/port.md](docs/port.md).</sub>

### One executable

`qinta` is the application, on Windows, macOS and Linux, and a default build produces nothing
else. Until 2026-10-07 the same project also defined `tinta`, upstream's Direct2D / DirectWrite /
Win32 app, as the reference the port was measured against. It was retired that day, with the
whole Direct2D stack: 13 source files, 8 headers and the 5 backend tests whose subject it was.
Its last `--printpages` output is kept as the reference it was (`build-win-split/s25ref/`, local
and untracked), and the port's history in [docs/port.md](docs/port.md) still describes the two
stacks side by side, because that is how the work was done.

`qinta.exe /register` registers qinta for `.md`, `.markdown` and `.mmd` on Windows, as
`Qinta.MarkdownFile` under its own `Software\Qinta\Capabilities`, so an upstream Tinta installed
beside it keeps its own registration (2026-10-07). As on every Windows 10 and 11 app, it then opens
Settings > Default apps, because only the user can make it the default. Off Windows, `/register`
is refused. `--dumpchrome` is gone for good with the DirectWrite chrome formats it read back.
There is no update check and no MSIX package.

## Building

**Qinta has no releases and no installer yet, but every push builds a package per platform.**
The latest successful run of [`build.yml`](.github/workflows/build.yml) on `master` carries
them as artifacts (downloading an artifact needs a GitHub login), each named by the commit it was
built from:

| Artifact | What it is | Checked in CI |
|---|---|---|
| `qinta-windows-x64` | A folder: `qinta.exe`, the Qt DLLs and the VC++ runtime DLLs | the five VC++ runtime DLLs are present, and a copy of the folder prints the fixture pages that are compared against MinGW's |
| `qinta-macos-app` | `Qinta.app`, Apple silicon, zipped | no library loads from Homebrew; unzipped, its signature verifies and it prints pages |
| `qinta-linux-appimage` | An x86_64 AppImage | every Qt library it loads is inside it; it prints pages in an Ubuntu 24.04 container with no Qt installed |

Not yet witnessed: any of the three opening a window on a machine that never had Qt or Visual
Studio installed, since CI only runs them headless. The macOS app is signed ad hoc and **not
notarized**, so macOS blocks it on first open; right-click it and choose Open. Building from
source, below, is the other way to get qinta.

All three platforms build from the same CMake project, and they are not equally proven. The
state per platform, before any commands:

| Platform | Target | State |
|----------|--------|-------|
| Windows | `qinta` — Qt 6 | Configures, builds, launches and stays running. Measured 2026-09-16. |
| Linux | `qinta` — Qt 6 | Configures, builds, every ctest entry passes, launches windowed and headless. Measured 2026-09-16 on Ubuntu 24.04.2 under WSL. |
| macOS | `qinta` — Qt 6 | Configures, builds, launches (plain binary, offscreen and windowed). Every ctest entry passes. Measured 2026-09-19 on macOS 26.6.2 (Darwin 25.6.0), arm64. |

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds every target and runs the
whole ctest suite on all three platforms, on each push to `master` and each pull request. The
table predates it, so a date in the table still means somebody ran it by hand that day, on the
toolchain named in the row. CI runs on GitHub's runners, not on those machines. Where a
command below is unverified it says so on the spot, rather than sitting in the same voice as
one that was measured.

No Direct2D or DirectWrite code is left to compile: the stack and its `TINTA_LEGACY_D2D` flag
were deleted on 2026-10-07.

### Prerequisites on every platform

- **Git**, to clone the repo and because CMake fetches the [MD4C](https://github.com/mity/md4c)
  parser with `git` during the first configure (`FetchContent`, pinned to release 0.5.2). That
  first configure therefore also needs network access to github.com.
- **CMake 3.15 or newer.**
- **A C++17 compiler.** MD4C itself is C11.
- **Qt 6** with the **Core, Gui, Widgets, Svg and Network** components, on every platform,
  Windows included. The configure stops without Core, Gui or Widgets, and `qinta` cannot link
  without Svg and Network.

Then, on any platform:

```bash
git clone https://github.com/atabisz/qinta.git
cd qinta
```

### Windows

**Prerequisites**

- **Visual Studio 2019 or newer** with the **Desktop development with C++** workload, which
  installs MSVC and the Windows SDK, which qinta's two Windows-only files (the file association
  and the title-bar chrome) and the Windows branches of the Qt backends build against. To build
  without Visual Studio, see [MinGW-w64](#windows-without-visual-studio-mingw-w64) below.
- **CMake 3.15+.** The copy Visual Studio bundles is fine, but it is not necessarily on `PATH`
  in an ordinary shell: it lives under
  `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`, which the Developer Command Prompt
  adds for you.
- **Git for Windows.**
- **Qt 6 for MSVC, 64-bit**: the `msvc2022_64` kit from the Qt Online Installer. Measured with
  Qt 6.8.3, whose kit already includes Widgets, Svg and Network. Without it the configure stops
  with an error that names Qt and the `CMAKE_PREFIX_PATH` to set.

**Build**

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
./build/Release/qinta.exe README.md
```

**`qinta.exe` does not find Qt's own DLLs by itself**, and that is the first thing to go
wrong rather than an edge case: launched with nothing else done it dies before `main()`, naming
`Qt6Core.dll` — `Qt6Cored.dll` from a Debug build. The exact wording is the shell's, not the
program's: measured in git-bash it reads `error while loading shared libraries: Qt6Core.dll`, and
another shell will phrase the same missing DLL its own way. Put
`C:\Qt\6.8.3\msvc2022_64\bin` on `PATH` in the shell you launch it from, or run Qt's
`windeployqt` against the binary once. The exit codes are readings, not guesses: `127` without
that directory on `PATH`, `124` under `timeout 5` with it, and the legacy `tinta.exe` read `124`
through the same harness while it existed — so `124` means *still running* rather than being what this harness
returns for everything.

### Windows without Visual Studio: MinGW-w64

MSYS2's UCRT64 toolchain builds qinta with GCC and MSYS2's own Qt 6, so no Visual Studio
licence is needed. **These are the steps CI runs on every push** (the `windows-mingw` job in
[`build.yml`](.github/workflows/build.yml)): the whole ctest suite passes, and the pages
`qinta --printpages` prints are byte-identical to the MSVC build's (see
[docs/port.md](docs/port.md#mingw-on-windows)). Not yet done by hand: opening the window of a
MinGW-built `qinta` on a desktop. CI only runs it headless.

Install [MSYS2](https://www.msys2.org/), open its **UCRT64** shell, and install the toolchain
and Qt:

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-svg
```

Then, from the clone, in the same shell:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/qinta.exe README.md
```

The UCRT64 shell has Qt's DLLs on `PATH`; started from anywhere else, `qinta.exe` needs
`C:\msys64\ucrt64\bin` on `PATH`. The tests also want `mingw-w64-ucrt-x86_64-ripgrep`, which
the README figure check needs, and then `ctest --test-dir build`. The MSVC build is still the
one CI packages, so the folder in the `qinta-windows-x64` artifact is MSVC's.

### Linux

**Prerequisites**

- **g++ 13+**, **CMake 3.15+**, **Git**, and the **Qt 6 base and Svg development packages**.
  Measured on Ubuntu 24.04.2 under WSL with g++ 13.3.0, CMake 3.28.3 and Qt 6.4.2. The package
  names below are Debian and Ubuntu's; other distributions have not been tried, and need their
  own equivalents of `qt6-base-dev` and `qt6-svg-dev`.

**Build**

```bash
sudo apt install git g++ cmake qt6-base-dev qt6-svg-dev

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
(cd build-linux && ctest --output-on-failure)

./build-linux/qinta README.md                      # a real window, with that file drawn in it
./build-linux/qinta                                # no argument: the placeholder line
QT_QPA_PLATFORM=offscreen ./build-linux/qinta      # headless: starts, paints, no window
```

**`qt6-svg-dev` is not optional, despite how the CMake reads.** Svg and Network are asked for as
`OPTIONAL_COMPONENTS` deliberately — so that a partial Qt install loses one test target and says
which one, instead of making `Qt6_FOUND` false and silently deleting every Qt target — but
`qinta` itself links `Qt6::Svg` (`target_link_libraries(qinta …)` in `CMakeLists.txt`), and linking a component that
`find_package` looked for and did not find is a **configure-time error, not a skip**. Measured
both ways on a scratch project: linking an absent component fails the configure with `but the
target was not found`, and linking a present one reaches `Generating done`. `qt6-base-dev`
carries Core, Gui, Widgets and Network; Svg is the one that needs a package of its own. Widgets
is not skippable either, and fails loudly on purpose: `QFileDialog` is the only non-Windows
implementation of the file-picker seam, so its absence is a `FATAL_ERROR` that names itself
rather than an undefined symbol at link time.

All three run lines above read `124` under `timeout 5`, meaning the process was still alive when
the timeout killed it. A path that cannot be opened prints `tinta: cannot open '<path>'` and
**still** opens the window on the placeholder — so a bad argument reads `124` as well, and is
not a refusal to launch.

### macOS

**Prerequisites**

- **Xcode Command Line Tools**, for `clang`/`c++`, the system headers CMake needs to configure at
  all, and `git`.
- **[Homebrew](https://brew.sh)**, for the other three: **CMake 3.15+** and Qt 6's **qtbase** and
  **qtsvg** formulae.

Measured 2026-09-19 on macOS 26.6.2 (Darwin 25.6.0), arm64, with Apple clang
21.0.0 (clang-2100.3.34.2), CMake 4.4.3, and Homebrew's `qtbase`/`qtsvg` 6.11.2. The meta
packages `qt` and `qt6` are not required and were not installed for this build.

**Build**

```bash
xcode-select --install                     # once, if `clang`/`c++` are not already present
brew install cmake qtbase qtsvg

cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix qtsvg)"
cmake --build build-mac -j"$(sysctl -n hw.ncpu)"
(cd build-mac && ctest --output-on-failure)

./build-mac/qinta README.md
```

- **The Qt requirement is identical to Linux's**: Core and Gui or the configure stops, Widgets
  or it stops again, and Svg and Network for `qinta` to link at all. Homebrew splits those
  across two formulae — `qtbase` (Core, Gui, Widgets, Network) and `qtsvg` — rather than one
  combined package, so `CMAKE_PREFIX_PATH` needs both prefixes.
- **You get a plain executable, not an app bundle.** The target carries `WIN32` and no
  `MACOSX_BUNDLE` — zero occurrences of it in the file — so `qinta` builds as a command-line
  binary here too. Confirmed: the built binary is a bare Mach-O executable, not a `.app`. To
  get one, run `scripts/package-macos.sh build-mac package`, which assembles `Qinta.app`
  around that binary with its Qt frameworks inside, the same step CI runs.
- **Every ctest entry passes**, after fixing four macOS-specific gaps the first build here
  surfaced — none of them were build or link problems, the binary configured and ran from the
  start:
  - `platform_paths`'s forced-locale control set `LANGUAGE` via `setenv()`, which Linux's glibc
    backend honours and macOS's does not: `QLocale::system()` there resolves through
    CoreFoundation/`NSUserDefaults` (System Settings), never consulting
    `LANG`/`LC_ALL`/`LANGUAGE`. The working macOS equivalent is `-AppleLanguages (tags)`, an
    argument-domain override read from argv at process start — CMakeLists.txt now passes it on
    the `platform_paths` test's command line on `APPLE`, and the exact-match assertion on the
    most-preferred tag was loosened to a primary-subtag prefix check, because macOS's canonical
    expansion order puts the maximized form (`zh-Hans-CN`) ahead of the literally-requested one
    (`zh-CN`) — both match the "zh" prefix the real caller (`detectSystemLanguage()`) actually
    depends on.
  - `platform_paths_bare_locale` has no macOS equivalent at all: every override CoreFoundation
    exposes (`LC_ALL=C`/`LANG=C`/empty `LANGUAGE`, an empty `-AppleLanguages ()`, `-AppleLocale
    C`) falls back to the real system language list rather than reporting bare `"C"`, so the
    state this control probes for is not reachable from a macOS test process. It is no longer
    registered there (`CMakeLists.txt`, `NOT WIN32 AND NOT APPLE`), rather than left failing
    against an unreachable target.
  - `qt_text` assumed DejaVu Sans, which Linux gets from Qt's own fontconfig setup but macOS
    does not ship at all — it silently substituted the system UI font instead, which is the
    exact un-asserted substitution this test exists to catch, and had no 'fi' ligature either.
    `kOnMacOS` now selects Helvetica for both the primary-family and ligature checks: it
    resolves to itself and measured glyph count 1 for "fi" (has the ligature), the same shape
    as the existing per-platform branches for Segoe UI/Calibri.
  - `qt_entry`'s idempotency check compared one pointer (`folderBrowserFormat`) before and
    after a second `installChromeFormats` call, expecting a fresh heap allocation to prove a
    real release-and-rebuild happened rather than a no-op. Measured: that single comparison is
    genuinely flaky on macOS's allocator — 2 passes then 3 fails across 5 consecutive,
    code-identical `ctest` runs — because a just-freed same-size block is sometimes handed
    straight back to the very next same-size request. The check now compares all ten format
    pointers and requires at least one to have moved; a real no-op still fails it (none would
    move), but one coincidental address reuse no longer does. Stable across 8+ repeated runs
    after the fix.

  `src/platform_paths_qt.cpp`'s `__APPLE__` branches (`_NSGetExecutablePath` for the executable
  directory, `~/Library/Application Support` for the config base, deliberately not
  `QStandardPaths`, whose generic config location is `~/Library/Preferences`) compile and pass.
- **The repo's own portability probe will not run here as written.**
  `scripts/probe-portable-linux.sh` calls `mktemp --suffix=.cpp` and
  `mktemp -t tinta-probe-XXXXXX.md`, both GNU forms that BSD `mktemp` (macOS's default) rejects.
  The `linux` in the filename is load-bearing. Not re-probed this pass.
- **Only this one configuration has been measured.** Apple clang 21 on macOS 26.6.2/arm64 via
  Homebrew Qt 6.11.2 — not Intel, not an older macOS or Xcode, not a non-Homebrew Qt install,
  and no `CMAKE_OSX_DEPLOYMENT_TARGET` was set, so the minimum supported macOS version is
  whatever CMake's default picks up from the host, unverified.

## Why Tinta — and why fork it?

Most markdown apps ship an entire browser to render text. Tinta uses the GPU-accelerated text stack already built into Windows:

|  | Tinta | Typora | Obsidian | VS Code |
|---|---|---|---|---|
| Startup | **<100 ms** | ~1.5 s | ~3 s | ~2 s |
| Install size | **~2.3 MB** | ~90 MB | ~250 MB | ~350 MB |
| Runtime | **Native Direct2D** | Electron | Electron | Electron |

It's a viewer first: perfect as the double-click default for `.md` and `.mmd` files, for reading documentation and diagrams — with an edit mode when you need it.

Those figures are upstream Tinta's, measured by upstream, on Windows. **Qinta has not been
benchmarked against anything** — no startup time, no binary size claim beyond what the build
happens to produce. Until it is measured, the table is left as Tinta's and marked as such.

The fork exists because that Windows-only stack is the whole reason for the speed *and* the whole
reason there is no macOS or Linux build. Reaching those platforms means either maintaining a
second stack — which is two of every feature, diverging quietly — or moving to one portable stack
everywhere. Qinta takes the second option. The cost is giving up the OS-native text stack; the
reasoning, and why Qt 6 rather than Skia, is in
[Stack choice: Qt 6](docs/port.md#stack-choice-qt-6).

## Features

- **No web engine** - Native Qt 6 rendering on Windows, macOS and Linux
- **10 beautiful themes** - 5 light and 5 dark themes to choose from
- **Word wrap** - Optional soft wrap in the editor (Ctrl+W)
- **Native LaTeX math** - `$inline$` and `$$display$$` equations with matrices, cases, aligned equations, fractions, limits, accents and custom macros. Uses an installed math font; no bundled fonts or web engine. See the [supported math syntax](docs/math-support.md).
- **Focused editing** - Hide the preview pane while writing (Ctrl+E)
- **Native Mermaid diagrams** - 22 diagram families, from flowcharts with subgraphs through sequence, class, state, ER, gantt, and pie to C4, sankey, kanban, and radar - rendered without a web engine
- **Export as HTML, DOCX, or PDF** - Right-click and pick "Export as..." for a self-contained web page, a Word document that opens natively, or a vector PDF; diagrams and math come along in every format
- **Pandoc export** - With [pandoc](https://pandoc.org) installed (or pointed at in settings), the editor offers EPUB, OpenDocument, PowerPoint, RTF and LaTeX too; rich formats convert from Qinta's own HTML export so diagrams and math survive as vector graphics
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
- **Tabs** - Win11 Notepad-style tabs in the title bar: files opened from Explorer join the window as tabs, Ctrl+Tab cycles, Ctrl+W or Ctrl+F4 closes (Ctrl+F4 also while editing, where Ctrl+W is word wrap), middle-click closes, Ctrl+T opens the file browser into a new tab, unsaved buffers show a dot, and dragging reorders tabs. Right-click a tab for close operations (close, close all but this, close all to the left/right) plus copy path and reveal in Explorer. Pulling a tab out of the strip floats it as a card: drop it on open space for a new window (which keeps the tab row), or onto another Qinta window to move it there — and dropping a single-file window onto a tab strip merges it back. Single-file windows stay tabless; turn the Explorer behavior off with the "Open files in tabs" setting
- **Folder browser** - Press B to browse and open Markdown or Mermaid files (Ctrl+click a file to open it in a new tab)
- **Table of contents** - Press Tab to see document headings, click to jump; the panel follows your reading position, and typing filters the headings
- **Unified editor** - Press `:` to edit: your raw Markdown on the left with the rendered page floating beside it as a sheet on the editor's desk. A slim line-number gutter, a soft accent wash on the caret's line and its rendered block, a tool rail with formatting controls plus table-size and diagram-template pickers, and Markdown assists (lists continue on Enter, Tab indents, Ctrl+B/I wrap). Search and find-replace work in the editor too
- **Emoji shortcodes** - `:rocket:` and friends render as real emoji
- **Fit to width** - Wide tables and diagrams grow a Fit button that shrinks them to the reading column
- **Draft recovery** - Unsaved quick notes are stashed continuously and offered back on the next launch
- **Create on click** - Clicking a missing file reference offers to create the file and opens it ready to type
- **Text-file references** - `.txt`, `.json`, `.yaml` and friends link like Markdown files and open as highlighted documents
- **Search** - Find text with F or Ctrl+F, cycle through matches with Enter; every match shows as a tick on the scrollbar
- **Persistent settings** - Remembers your theme, zoom level, and window position
- **Portable mode** - Create a `settings.ini` next to the `qinta` executable (an empty file works) and the whole configuration — settings, custom themes, languages, drafts — lives beside the executable and travels with it; without one, everything stays in `%APPDATA%\Tinta` on Windows, `$XDG_CONFIG_HOME/Tinta` (usually `~/.config/Tinta`) on Linux and `~/Library/Application Support/Tinta` on macOS
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
change a value and restart Qinta. Accepted values are letters, digits,
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
(also in Settings → General). `qinta --new` starts an untitled quick note
from the command line.

The interface language is available in Settings → General. It follows the
Windows display language by default, or can be changed explicitly. Transient
notifications and native error dialogs use the same language. Additional
translations can be added through `languages.ini`.

## Cross-platform port

Qinta runs on Windows, macOS and Linux from one Qt 6 stack. The goal, the stack choice, how the
port is structured, how to build and test it off Windows, and the measured status of every slice
are in [docs/port.md](docs/port.md).

The version is **0.0.1**: qinta's numbering restarted there, and 1.0.0 is the release where the
port is finished and the Windows-only functionality is gone. What is left before then is in
[docs/roadmap.md](docs/roadmap.md).

## Usage

```bash
# Open a Markdown file
qinta document.md

# Open a Mermaid flowchart
qinta diagram.mmd

# Open with light theme
qinta -l document.md

# Show stats on startup
qinta -s document.md

# Start an untitled quick note, or open a file straight into the editor
qinta --new
qinta --edit document.md

# Export without opening a window (the argument is the OUTPUT path)
qinta --exportpdf document.pdf document.md

# Register for Markdown and Mermaid files (Windows only)
qinta.exe /register
```

Or simply drag and drop a `.md` or `.mmd` file onto the window. The binary is `qinta.exe` on
Windows; [Building](#building) has how to launch it per platform.

To copy a document's full path, right-click the document or its tab (the filename
in the title bar when only one file is open) and choose
**Copy file path**, beside **Reveal in Explorer**. The tab menu copies the clicked
tab's path, even if another tab is active. Untitled notes have no path until saved.

## File Association

On first launch on Windows, Qinta will ask if you want to set it as the default viewer for Markdown and Mermaid files. If you choose "No", you won't be asked again.

To register Qinta as the default viewer later, run:
```bash
qinta.exe /register
```

This registers `.md`, `.markdown`, and `.mmd` so you can select Qinta as their default app in Windows Settings.

## Mermaid Support

Qinta natively renders twenty-two Mermaid diagram families in `.mmd` files and fenced `mermaid` code blocks — no web engine involved:

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
HTML and DOCX exports. Native printing keeps the existing light print palette.

## Dependencies

- [MD4C](https://github.com/mity/md4c) - Fast markdown parser (fetched automatically by CMake)
- Qt 6 — the `qinta` target on all three platforms. **Core** and **Gui** are required off
  Windows and the configure stops without them; **Widgets** likewise, for the file-picker seam;
  **Svg** and **Network** are asked for optionally but are needed for `qinta` itself to link.
  On Debian and Ubuntu that is `qt6-base-dev` plus `qt6-svg-dev`; on macOS, Homebrew's `qtbase`
  plus `qtsvg` (the `qt`/`qt6` meta-formulae are not required).
- Xcode Command Line Tools — macOS only, for `clang`/`c++` and the system headers CMake needs
  to configure at all.

## License and attribution

MIT, unchanged from upstream — see [LICENSE](LICENSE), which keeps its original copyright line
(`Copyright (c) 2024 oipoistar`).

Qinta is a fork of [oipoistar/tinta](https://github.com/oipoistar/tinta). The viewer, the themes,
the Mermaid and math support, the editor and the exporters are upstream's work; what this fork
adds is the move to a single portable stack. Nothing here is offered back upstream, and the
`upstream` remote in this repository is deliberately configured with its push URL disabled.

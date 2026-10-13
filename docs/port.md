# Cross-platform port (Windows / macOS / Linux)

> **The Direct2D stack was retired on 2026-10-07**, and with it `tinta`, the Windows app this
> section measures `qinta` against. Read everything below as the record of how the port was
> done: where it says `tinta`, `main_d2d.cpp`, a `_d2d` file or `TINTA_LEGACY_D2D` in the
> present tense, it describes the tree before that day. The figures the README gate checks
> are re-derived from the tree as it is now. Every figure in this section is measured by a
> script in the repo rather than estimated.

![qinta drawing a markdown file on the Qt stack](qinta-windows-first-document.png)

*The Qt build, `qinta some.md`, on Windows — headings, prose, inline code, bold and
italic, a link, a fenced code block with syntax highlighting and a blockquote, none of it
through Direct2D or DirectWrite. The caption here used to say "**look at the bottom edge**:
the document is cut off mid-heading and there is no scrollbar, because there is no input
path yet" — corrected in place rather than deleted, because it named the exact distance to
go and the next picture is what closed it. There is still no scrollbar; the document is no
longer stuck at the top.*

![qinta scrolled to the end of this README on the Qt stack](qinta-windows-scrolled-to-end.png)

*The same binary showing the **end** of this README — `qinta README.md`, then End,
through the real Windows keyboard. Page Down ×3 changed 40,087 sampled pixels, End changed
42,906, and Home came back to a capture that differs from the first by **0 pixels**, both
PNGs 26,652 bytes. The round trip is the reading worth having: a paint that drew something
plausible at each offset would satisfy every one-way comparison and fail this one.*

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
dependency becomes a small abstract interface with a header per backend. **Where a
conditional genuinely is the right tool — two of them survive inside
`src/paint_document.cpp` — it is spelled `#ifdef TINTA_D2D_BACKEND`, not `#ifdef _WIN32`,
because the question is which backend is linked and not which OS is compiling.** The
"transitional" column is what keeps Windows working today and is scheduled for deletion;
the "target" column is where every platform ends up:

| Seam | Replaces | Transitional (Windows; every one deleted by 2026-10-07) | Target (all platforms) |
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

**The per-platform recipes now live in [Building](../README.md#building)** — Windows, Linux and macOS,
each with its run lines — and are deliberately not repeated here, because
a second copy of a build recipe is a second thing to keep true. This section is what those
commands do not tell you: which requirements are hard, which are not, and what the tests pin.
macOS is built with Apple clang and its whole ctest suite run on every push since CI landed on
2026-09-29 (the configuration is in [macOS](../README.md#macos)). That is one measured
configuration, not a supported range.

Qt 6 is **required on every platform.** Core and Gui are a `FATAL_ERROR` when missing, because
the path, keyboard, clipboard, process and image seams have no non-Qt half; on Windows Widgets is
required as well, because `qinta` is the Windows app.

The headless targets pin `QT_QPA_PLATFORM` themselves, and it is pinned **per platform, not
once**: `offscreen` off Windows, `windows` on Windows, from one `TINTA_QPA_PLATFORM` variable in
`CMakeLists.txt` — cited by name because the line number that stood here was wrong by nearly
three hundred lines, and a `file:line` in prose is a claim with an expiry that no gate watches.
That asymmetry is measured, not stylistic — the offscreen plugin on
Windows has **no font database**, so a text assertion there measures a fabricated metric
rather than a glyph. `minimal` is used nowhere, for the same reason and more so, and running
with no platform plugin at all aborts under WSL.

```bash
# just the frontend, without the test targets, on any of the three platforms
cmake --build build-linux --target qinta
```

The launch lines, and the exit codes they were read at, are in [Building](../README.md#building) per platform.

`qinta` takes the same switches `tinta` did, through one `parseLaunchOptions` chain:
`--printpages`, `--exporthtml`, `--exportdocx` and `--exportpdf` write their output and exit without
opening a window, behind an output guard, so `qinta --exportpdf README.md` refuses rather than
overwriting the document. `-l`, `-s`, `--new`, `--edit`, `--tabbed`, `--pos` and `/register` are
honoured; `--dumpchrome` was removed on 2026-10-11, because the chrome dump went with the Direct2D stack.
An unknown flag, or a flag missing its argument, is reported on stderr rather than silently
ignored. A path that cannot be opened prints `qinta: cannot open '<path>'` on stderr, and the
window still opens.

`qt_text`, `render_api` and `qt_entry` exist *only* where Qt does. `keyboard` is the exception and
deliberately so: it is configured under `if(WIN32 OR Qt6_FOUND)`, because the seam has two
backends and a seam whose Windows half is never asserted is precisely the shape this repo
has criticised elsewhere. Since 2026-10-07, when `keyboard_win32.cpp` was deleted, every
platform links `keyboard_qt.cpp`.

CI runs ctest but not this probe, so the probe is green only if somebody ran it. The regression guard is
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

### The Windows C runtime: dynamic, with the DLLs beside the exe

**Decided 2026-10-07: option B below, app-local runtime DLLs.** Upstream Tinta linked the
**static** CRT so its exe ran on a clean Windows install with no VC++ redistributable, which is
what kept it a single file you can copy. **Qt cannot be linked against the static CRT**, and once
the clipboard, keyboard, paths and process seams collapsed onto Qt nearly every target in the tree
linked Qt on Windows too. So the `if(MSVC)` block at the top of `CMakeLists.txt` now selects the
**dynamic** CRT for the whole tree, and `qinta` ships as a folder rather than a file.

The options were:

- **A. Dynamic CRT plus the VC++ redistributable as a prerequisite.** Smallest folder, but the
  folder no longer runs where you copy it: it needs an installer step first.
- **B. App-local runtime DLLs** (chosen). The CI Stage step copies Visual Studio's
  `Microsoft.VC*.CRT` folder (2.0 MB, measured against VS 18's 14.51 redist) beside `qinta.exe` and
  the Qt DLLs, and fails if `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`,
  `msvcp140_1.dll` or `msvcp140_2.dll` is missing; those five are what `qinta.exe`, `Qt6Core`,
  `Qt6Gui` and `Qt6Widgets` import. The UCRT itself ships with Windows 10 and 11. So the folder
  runs where you copy it, which is as close to the single-file promise as a Qt app gets.
- **C. A static Qt build.** One exe again, but it means building Qt from source in CI, and static
  linking under Qt's LGPL obliges the project to let users relink. Not pursued.

**`windeployqt --compiler-runtime` does not do B for MSVC**, which is easy to assume. Measured
2026-10-07 with Qt 6.8.3: with `VCINSTALLDIR` set it copies `vc_redist.x64.exe`, the installer,
and without it it copies nothing. So CI passes `--no-compiler-runtime` and copies the DLLs itself.

**Not yet witnessed:** `qinta.exe` starting from that folder on a machine with no VC++ runtime
installed. This machine and the CI runners both have one, so a launch here cannot tell an
app-local DLL from the system copy. A MinGW build
([below](#mingw-on-windows)) would change the question
again: it uses the UCRT and no VC++ runtime at all.

### MinGW on Windows

The plan is to build Windows with **MinGW-w64** instead of MSVC, so that building qinta no
longer needs a Visual Studio licence. Linux (GCC) and macOS (AppleClang) already build without
one; Windows is the only platform that does not.

MinGW is the route because it is the only one that removes the licence. `clang-cl` would make the
compiler free (Apache 2.0), but it still compiles against the MSVC STL, the CRT headers and
libraries, and the Windows SDK, and those come with a Visual Studio or Build Tools install.
MinGW-w64 supplies its own headers and import libraries and links against the C runtime Windows
itself ships (UCRT); `llvm-mingw` (Clang) and MSYS2's GCC are both
candidates.

**It waited for the Direct2D and DirectWrite code to be removed, and that happened on
2026-10-07.** MinGW's headers have historically lagged Microsoft's for the recent Direct2D and
DirectWrite interfaces (`d2d1_3`, `dwrite_3`), so tinta was the part most likely to fail to
build, and it was deleted rather than ported. Only qinta is left, so the switch now costs:

- a **MinGW Qt kit** (Qt's `mingw_64`, or MSYS2's `qt6`) in place of `msvc2022_64`, locally and
  in CI;
- GCC-style equivalents of the `if(MSVC)` block in `CMakeLists.txt`, which MinGW does not enter:
  `/W4`, `NOMINMAX` and `_CRT_SECURE_NO_WARNINGS` (the CRT setting does not apply, because MinGW
  links the UCRT). `NOMINMAX` is in place since 2026-10-07, as an `elseif(WIN32)` arm of that block;
- re-running the full `ctest` suite and the rendering harnesses under the new toolchain.

It also changes the C runtime question above: a MinGW build does not use the VC++ runtime, so
what ships next to the exe becomes the MinGW runtime (`libstdc++`, `libgcc`, `libwinpthread`, or
linked statically) plus the Qt DLLs.

**Started 2026-10-07 as a CI job, not a switch.** `.github/workflows/build.yml` has a
`windows-mingw` job: MSYS2's UCRT64 GCC and Qt 6, configure, build and the whole `ctest` suite. It
was `continue-on-error` until it had been green, so a red there was a finding rather than a broken
`master`. **It went green on 2026-10-11, and since then it runs on every push and a red fails the
run**, the same as the other three hosts.

**Decided 2026-10-11: MinGW is a supported way to build on Windows, and MSVC stays the build that
ships.** The kit and the re-run above are done: the whole suite passes under MinGW and the
harnesses agree (below). The `if(MSVC)` block's GCC equivalents are done only in part: `NOMINMAX`
is in, and there is no `/W4` counterpart, so MinGW builds at GCC's default warning level, as the
Linux and macOS builds do. That is the licence goal met, since a warning level is not what it
needed: anyone can now build qinta on Windows without Visual
Studio, through the steps in README's
[MinGW-w64 section](../README.md#windows-without-visual-studio-mingw-w64). Changing the shipped
folder as well would buy nothing for the people downloading it, since CI's runners carry Visual
Studio, and would swap the VC++ runtime beside the exe for MinGW's, which nobody has launched on a
clean machine either.

**First reading, 2026-10-07 (run 37585526316 on `160ff18`): MinGW configures and builds the whole
tree, and 50 of 52 ctest entries pass**, including the Qt app tests, `qinta_cli`, clipboard and
`file_association`. Two failed:

- `copy_file_path`: two of its six Windows path cases resolve differently under libstdc++'s
  `std::filesystem`, where MSVC's `absolute()` goes through `GetFullPathNameW`. That is product
  code (`absoluteFilePath` in `src/utils.cpp`, behind Copy file path and Reveal), so it is the
  one real port finding. The test now prints the failing input, the expected path and the one it
  got, and the second run (37590610492, on `553c998`, 51 of 52) named them: the UNC path
  `\\server\share\folder name\document.md` came back as `D:\server\share\folder name\document.md`,
  and the `\\?\C:\...` long path as `D:\?\C:\...`. libstdc++ reads a leading `\\` as a root
  directory with no root name and puts the current drive in front, so a MinGW build would copy a
  wrong path for any document on a network share. **Fixed 2026-10-11**: the resolution moved into
  the paths seam as `platform::absolutePath`, which on Windows is `GetFullPathNameW`, the call
  MSVC's `absolute()` makes. Checked with a MinGW-built probe (GCC 13, run natively on Windows)
  over the same six cases: the old code missed the same 2 (with `C:` in front locally), the new
  code missed 0. MSVC and Linux pass `copy_file_path` unchanged. Re-read from the CI job on
  2026-10-11 (run 38092855683, on `473a6b9`, GCC 16.2.0 and Qt 6.11.2): `copy_file_path` passes,
  and so does the **whole suite, 53 of 53**.
- `readme_figures`: not a MinGW defect. MSYS2's bash has no `/dev/fd` when ctest starts it, so
  the gate's `comm <(...)` calls read nothing, and the to-do header passed on an empty set. The
  gate now goes through temp files and fails when `comm` does.

**The rendering harnesses agree, 2026-10-11 (run 38099623195, on `e9491b5`).** Both Windows jobs
print five fixtures through `qinta --printpages` (README, `markdown-regression-control`,
`math-mixed-layout`, `math-inline-stress`, `supsub-probe`), and the `print-pages-agree` job compares
them with `scripts/compare_print_pages.py`: **175 of 175 pages are byte-identical**, though the two
jobs install different Qt releases (6.8.3 and 6.11.2). The same pages printed on a local Windows
machine differ from CI's on 23, so the comparison can see a difference. A different page count or
page size fails the run from now on; pixel differences are reported without failing, because a Qt
release can move glyph rasterisation.

### Status

One row per slice, newest last, each with its own date. The detail of each slice, and the measurements
taken along the way, are in the commit history.

| When | Slice | State |
|---|---|---|
| to 2026-09-17 | Text, render, Win32 type and call, paths, keyboard and `Seated<>` seams; image, path-geometry, line-break, FILETIME and file-picker seams; HTML export | Done |
| 2026-09-16 → 17 | `qinta` draws a document from its argument, scrolls, opens a second one, and has chrome formats and a scrollbar | Done |
| 2026-09-17 | `overlays.cpp` compiles off Windows; `utils.cpp` split | Done |
| 2026-09-18 | `editor.cpp`, `drafts.cpp`, `signals.cpp` and `startpage.cpp` compile off Windows; document and chrome text formats and typography go through the seam | Done |
| 2026-09-19 | The last native bitmap, stroke and unwrapper handles leave `App`; per-platform default font; clipboard image half; first macOS build, with its four ctest failures fixed | Done |
| 2026-09-20 → 21 | `input.cpp` compiles off Windows; process, PDF, tab-ghost and zen-mode seams; launch options leave `WinMain` | Done |
| 2026-09-23 → 24 | `tabs.cpp` compiles off Windows; the `input.cpp` link closure closes; `TINTA_QT_UI` | Done |
| 2026-09-25 → 28 | Every Qt key, mouse and wheel event goes through `input.cpp`; timers and posted work fire on `qinta` | Done |
| 2026-09-29 | Close checks unsaved buffers; theme and geometry saved; follows system light/dark; themed title bar on Windows; CI on all three platforms; Windows builds `qinta` by default | Done |
| 2026-10-06 | `print.cpp` compiles off Windows: print preview, PDF export and the link peek work on `qinta`; Print followed in the QPrinter slice | Done |
| 2026-10-06 | `qinta` keeps a render target outside paint, as `tinta` always has: remote images, images laid out from input, and .docx diagrams work | Done |
| 2026-10-06 | `qinta --printpages`, `--exporthtml`, `--exportdocx` and `--exportpdf` work as headless switches behind the output guard | Done |
| 2026-10-06 | Print works on `qinta`: `QPrintDialog` and `QPrinter`, with the CJK print fixture the arm was waiting for | Done |
| 2026-10-06 | New windows on `qinta`: Ctrl+Shift+N, a note in its own window, and `--new`, `--edit`, `--stats`, `--tabbed` | Done |
| 2026-10-06 | The tab strip's always-on-top pin works on `qinta` (`Qt::WindowStaysOnTopHint` through the window sink) | Done |
| 2026-10-06 | A second `qinta` launch joins the running window as a tab, over `QLocalSocket`, under tinta's single-instance rule | Done |
| 2026-10-06 | Tear a tab off into its own window on `qinta`: detach under the Qt ghost, drop away from the window | Done |
| open | Moving a tab between two running `qinta` windows, BOTH directions — a torn-off tab dropped onto another window, and a single-file window dropped onto another window's strip (`tabWindowDropMerge`, a stub with no caller). One cause: no portable "which process owns the window under the cursor". On Qt the first drop opens a new window and the second does nothing | Not attempted |
| 2026-10-06 | `qinta` draws tinta's whole frame: start page, editor, tab strip, search, every overlay and dialog (`src/render_frame.cpp`) | Done |
| 2026-10-06 | `qinta` shows one row of caption buttons: the tab strip leaves minimise, maximise and close to the native title bar | Done |
| 2026-10-07 | Slice 25: retire `tinta` and delete the Direct2D stack — `main_d2d.cpp`, `d2d_init.cpp`, `chrome_dump_d2d.cpp` and ten more files, 4,821 lines, nothing ported | Done |
| 2026-10-07 | `/register` on `qinta`: `qinta.exe` owns the `.md`, `.markdown` and `.mmd` association as `Qinta.MarkdownFile` | Done |
| 2026-10-07 | Collapse the four Win32 seam halves that had a Qt half — `clipboard_win32.cpp`, `keyboard_win32.cpp`, `platform_paths_win32.cpp`, `platform_process_win32.cpp` — onto `clipboard_qt.cpp`, `keyboard_qt.cpp`, `platform_paths_qt.cpp` and `platform_process_qt.cpp`, on every platform. Explorer still selects the file on reveal; the tree-wide CRT is dynamic | Done |
| 2026-10-07 | `settings_win32.cpp` (the registry, for `/register`) and `window_chrome_win32.cpp` (DWM caption colours) are Windows by subject. They are listed in `tests/windows-only-sources.txt`, counted in their own row and in no to-do figure, and the figure gate refuses an entry that is not an existing `*_win32.cpp` off the portable manifest | Done |

What the **To do** count below is made of is derived, not listed by hand:
`scripts/check-readme-figures.sh` takes every `src/*.cpp` that is not in
`tests/portable-sources.txt`, so the table above deliberately carries no line counts.

**To do — 0 files, 0 lines**

**The to-do set emptied on 2026-10-07, from 6 files and 1,006 lines.** `clipboard_win32.cpp` (321), `keyboard_win32.cpp` (29), `platform_paths_win32.cpp`
(284) and `platform_process_win32.cpp` (69) were the Win32 halves of seams whose Qt halves already ran
on Linux and macOS. `qinta` builds the Qt halves on Windows now, and the four files are deleted: 703
lines, nothing ported, because there was nothing left to port. Two files remain and both are Windows
by subject: `settings_win32.cpp` writes the file association to the registry and
`window_chrome_win32.cpp` colours the title bar through DWM. Neither has anything to be ported to, so
they are listed in `tests/windows-only-sources.txt` and counted in the table below
instead of here: 2 files, 303 lines. The figure gate refuses an entry on that list unless it exists, is named
`*_win32.cpp` and is not on the portable manifest, so the list cannot be used to hide a file. Bases
A to C keep their full denominators, so this changes no basis by definition.

**The off-Windows census of the files not on the manifest.** `scripts/census-nonportable-linux.sh`
compiles every `src/*.cpp` that is not on `tests/portable-sources.txt` with g++ off Windows.
`settings_win32.cpp` is now the only one of the files that fail out of a census of **2**, and the
second is `window_chrome_win32.cpp`, which compiles to an empty translation unit off Windows. Both
are Windows-only by design. **The census reports what the compile emitted, and `clean` excludes the empty ones:** `clean: 0   vacuous: 1   fails: 1`, controls 11/11, measured 2026-10-07
(`vacuous` = the object exports no external symbol and carries no `.text` bytes). The figure gate
derives the denominator from the tree and fails if the published split does not sum to it, but it
cannot run g++, so it can never tell you that `fails: 1` is true: `clean: 1   vacuous: 0   fails: 1`
would be green and false. That is why it finds the published figure by the sentence that introduces
it rather than by its shape.

**What is left, by file.** Both line figures are re-derived by the figure gate:

| File | Lines | Note |
|---|---|---|
| `render.cpp` | 3,496 | Compiles, links and runs off Windows. Kept here because it is the file the native-mention table under **How complete is it?** tracks |
| `settings_win32.cpp` + `window_chrome_win32.cpp` | 253 + 50 | Windows by subject: the file association in the registry, and the DWM title-bar colours. Listed in `tests/windows-only-sources.txt` |

Under one stack there is no permanent Windows half *of a seam*, so the old "will never move"
category was work, and on 2026-10-07 the last four seam halves were deleted. What is left is not a
seam half at all, **303 lines of it today**, and it is listed by name and checked rather than
excluded from the count on argument.

### How complete is it?

A single number would be misleading here, because "compiles", "is tested" and "runs"
are three different milestones and the port is at very different points on each. Every
basis below is measured, on Windows and Linux:

> Every countable figure in this section is re-derived from the tree by `bash
> scripts/check-readme-figures.sh`, which fails when this page says something else. It runs as
> two ctest arms, `readme_figures` and `readme_figures_control`; the control perturbs every
> derived value by one and passes only if every claim then misses, so a green run cannot be
> `grep` finding something by accident. The arms also check the number of claims the gate
> printed, because one claim needs ripgrep's PCRE2 and is skipped without it. `ctest -LE prose`
> runs the suite without them.

| Basis | Measured | |
|---|---|---|
| A. Source lines free of the Windows stack | 54,294 / 66,652 | **81.5%** |
| B. `src/*.cpp` files compiling without Windows headers | 79 / 81 | 97.5% |
| C. Files a running test links **off Windows** | 78 / 81 | 96.3% |
| D. Test targets that *could* move, and do | 53 / 54 | 98.1% |
| E. All test targets, run off Windows | 53 / 55 | 96.4% |

- **A** is source lines in the `src/*.cpp` files on `tests/portable-sources.txt`, over every
  `.cpp` and `.h` under `src/` and `include/`.
- **B** is the files on that manifest, over every `src/*.cpp`.
- **C** is derived by `scripts/probe-portable-linux.sh`: the files on a running test's link line
  off Windows, minus the ones linked but never called.
- **D** and **E** come from `tests/test-platforms.txt`, which the gate checks against the Windows
  ctest list in both directions.

**Basis A cannot reach 100%.** Its numerator sums only `src/*.cpp`, while its denominator also
counts headers, so a header line is in the denominator permanently and can never enter the
numerator. With `src/*.cpp` totalling 54,597 lines of the 66,652,
**basis A's ceiling is 81.9%, not 100%**, and basis A should be read against that. Every new
header lowers the ceiling slightly.

**The test targets that run on Windows only**, with the count of `src/*.cpp` each one compiles
that are outside `tests/portable-sources.txt`, measured against CMake's generated project files:

| target | blockers | what they are |
|---|---|---|
| `config_paths` | 0 | **nothing** — Windows-only by *subject*: MSIX filesystem virtualization, and its own test includes `<windows.h>` for `CreateFileW` |
| `file_association` | 1 | `settings_win32.cpp` — the registry code under test, which has no non-Windows form. `window_chrome_win32.cpp`, the DWM title bar, counted here too until 2026-10-11; it now reaches every app test through the shared `qinta_app` objects, so it is no longer this target's own source, and those tests run off Windows as well. It read 5 until the clipboard, keyboard and process seam halves were deleted the same day. Added 2026-10-07: `/register` run for real against a redirected HKCU |

**`render.cpp`'s native mentions.** Counted with the pattern
`(ID2D1|IDWrite|IWIC|D2D1_|DWRITE_|WIC[A-Z])[A-Za-z0-9_]*` plus `D2D1::`, re-derived at every
commit rather than carried forward:

| Pass | `render.cpp` lines | Mentions | Excluding whole-line comments |
|---|---|---|---|
| `4e950a4` text path onto the seam | 3,498 | 100 | 85 |
| `092afcd` image seam | 3,385 | 75 | 60 |
| `cc292d4` path geometry seam | 3,379 | 55 | 38 |
| `d1351e8` line-breaking seam | 3,301 | 38 | 22 |
| current tree | 3,496 | 15 | 0 |

The same numerals recur with different meanings in that table, so read the column, not the digit.
"Excluding whole-line comments" drops whole-line `//` comments only. The remaining mentions are all
comments recording what each seam replaced, and they are kept on purpose: a seam is only
auditable while the thing it stands in for is still named somewhere.

**0 direct `dwriteFactory->CreateTextLayout` calls remain in the tree**. A
`grep` for the call returns 3, and all three are comments, not calls.

**What is left is Windows by subject.** The remaining 303 lines of Windows backend are
`settings_win32.cpp` (the file association, in the registry) and `window_chrome_win32.cpp` (DWM
title-bar colours), listed in `tests/windows-only-sources.txt`. Neither has anything to be ported
to. Re-verifying Windows on the new stack is effort that appears in no line count here.

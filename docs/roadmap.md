# Road to 1.0.0

Qinta's numbering restarted at **0.0.1** on 2026-10-11. **1.0.0** is the release where the port to
one Qt stack is finished and the Windows-only functionality is gone, either ported to all three
platforms or removed. Each item below says what is measured and what is not. Take a figure as of
the date given, not as a live reading.

## Porting: behaviour the retired Windows app had and qinta does not

- [x] **Done 2026-10-11.** Every field WinMain applied at launch and wrote at close is restored
  again (`applySavedPreferences` and `storePreferences`, `src/settings_app.cpp`), traced from the
  retired `WinMain`'s own load and close blocks rather than from a census. The UI language was
  among the gaps: qinta never set it at launch. Test: `settings_restore`. The original item:
  **Find which saved settings qinta does not restore, then restore them.** `settings.ini` holds
  34 fields. The retired `WinMain` applied most of them in one block after loading; qinta's launch
  path (`main_qt.cpp`) reads 11 directly, and others persist and restore through their own routes:
  recent files, per-document reading position and zoom (`findReadingPosition`, `findReadingZoom`),
  the editor mode, open-in-tabs and the theme. **The size of the gap is not measured yet.** A census
  of field names on 2026-10-11 was refuted twice by routes it could not see, so the slice that does
  this starts by tracing each field from load to use. The candidates that census found no
  reader for: reading and zen width, word wrap, the editor preview, heading rules, folder search,
  and the table-of-contents and browser pins.
- [x] **Session restore. Done 2026-10-11:** a plain launch reopens the last session's tabs, and the
  session-owning window writes them at close (`tinta::sessionToRestore`, `src/qt_launch.cpp`).
  Checked in the real binary: a seeded three-tab session reopened with its active tab, and a normal
  close wrote it back. Fixture: `tests/fixtures/session-restore.md`.
- [x] **Removed 2026-10-11**: the fields, the three settings keys, the chip's action, close action,
  icon and string, the timer and message IDs. An old `settings.ini` still loads, and its next save
  drops the keys (`settings_restore`). An update check against qinta's own releases belongs with
  publishing, below. The original item: **The update check.** It was retired with the Direct2D stack: nothing sets `updateAvailable`
  any more. The `checkUpdates`, `lastUpdateCheck` and `dismissedUpdate` settings and the
  "update available" action remain, and that action opens **upstream's** release page
  (`src/signals.cpp`). Either reimplement it against qinta's own releases or remove all of it.
- [x] **Removed 2026-10-11**: the switch, its field, its place in the output guard and its refusal.
  It is an unknown option now, and the path after it is an ordinary argument, not an output
  (`launch_options_tests`). The original item: **`--dumpchrome`** is refused (`src/main_qt.cpp`): its body was `chrome_dump_d2d.cpp`, deleted
  with the Direct2D stack. Port it onto the Qt render target or delete the switch.
- [ ] **IME candidate window position.** `editorPositionImeWindow` (`src/editor.cpp`) is a no-op on
  qinta, so CJK composition works but its candidate list does not follow the caret. Qt's route is a
  `QWidget::inputMethodQuery(Qt::ImCursorRectangle)` override on the editor's widget.
- [ ] **Dropping a torn-off tab onto another running qinta window.** Open in
  [port.md's Status](port.md#status): Qt has no portable way to ask which process owns the window
  under the cursor, so on qinta that drop opens a new window instead.
- [ ] **Retire `TINTA_QT_UI`.** It split the Win32 window arms from the Qt ones while both stacks
  existed. No line in `src/` or `include/` tests it any more (0 preprocessor lines, against 51 for
  `_WIN32` through the same search, 2026-10-11), so it is a definition nothing reads. Remove it
  from `CMakeLists.txt`, and correct the comments that still describe guards built on it, such as
  the header of `include/win32_api.h`, which has no conditional arms left.

## Windows-only by subject: decide, then document

- [ ] **`/register`, the file association**, works on Windows through the registry
  (`src/settings_win32.cpp`) and is refused elsewhere. The packages now declare the `.md` types
  themselves (`CFBundleDocumentTypes` in the macOS app, `MimeType` in the Linux desktop entry),
  which may be the whole answer off Windows. If it is, say so, and change the refusal's message.
- [ ] **The themed title bar** (`src/window_chrome_win32.cpp`) colours the caption through DWM, the
  only API Windows has for it. Keep it as a Windows integration, or drop it for Qt's default frame.
  It is in `tests/windows-only-sources.txt` with `settings_win32.cpp`, the two files the porting
  figure counts as Windows by design.

## Releases

- [ ] **Publish v0.0.1**: tag it, and add a release job that attaches the three packages
  `build.yml` already builds on every push (README § Building) to a GitHub Release.
- [ ] **Notarize the macOS app.** It is signed ad hoc and not notarized, so macOS blocks it on first
  open until the user right-clicks it and chooses Open. Notarizing needs an **Apple Developer
  Program membership** (US$99 a year), a Developer ID certificate in the repository's secrets, and
  a `notarytool` step after `scripts/package-macos.sh` signs the bundle.
- [ ] **Witness each package opening a window** on a machine that never had Qt or Visual Studio
  installed. CI runs all three headless only.
- [ ] **Open a MinGW-built qinta's window by hand** on a Windows desktop. CI runs that build
  headless only, too.

## Quality

- [ ] **A warning level for GCC and Clang.** MSVC builds at `/W4`. MinGW, Linux and macOS build at
  the compilers' defaults, because the `if(MSVC)` block in `CMakeLists.txt` has no counterpart.
  Expect a first pass to surface warnings nobody has read.

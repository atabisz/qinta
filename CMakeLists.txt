cmake_minimum_required(VERSION 3.15)
project(tinta VERSION 3.6.7 LANGUAGES C CXX)

# THE VERSION DEFINES ARE A LIST BECAUSE MORE THAN ONE TARGET NEEDS THEM, and that became
# true the moment a file reading TINTA_VERSION_* was measured off Windows. src/overlays.cpp
# formats "v%d.%d.%d" from these three, and its own comment already called them "the single
# source of truth" -- which they were not, because they reached exactly one target. Off
# Windows the census read three `was not declared in this scope` errors that had nothing to
# do with portability: the file was fine and the target was missing a define.
#
# IT LIVES HERE, DIRECTLY UNDER project(), AND THAT PLACEMENT IS THE WHOLE POINT. Written
# first beside its only consumer -- target_compile_definitions(tinta ...) -- which put it
# inside that block's if(WIN32), so off Windows the variable was EMPTY and both new consumers
# expanded to `target_compile_definitions(<target> PRIVATE )`. CMake accepts that silently:
# no warning, no error, a configure and a build that both succeed, and three version errors
# still in the census. Caught only because the re-measurement script greps the definition off
# qinta's actual compile line and printed "NO VERSION DEFINES ON THE LINE". A single
# source of truth placed inside a platform guard is not one.
#
# Applied to tinta, qinta and portable_compile_only rather than inherited via
# $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>, which some test targets below do use: that
# expression would also drag TINTA_D2D_BACKEND onto qinta, and qinta is precisely the
# target that must NOT have it.
set(TINTA_VERSION_DEFINITIONS
    TINTA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TINTA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TINTA_VERSION_PATCH=${PROJECT_VERSION_PATCH})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

include(CTest)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    # Dynamic CRT, tree-wide, since 2026-10-07. Upstream Tinta linked the static CRT so
    # its exe ran on a clean Windows install with no VC++ redistributable, but Qt cannot
    # be linked against the static CRT, and once the platform seams collapsed onto Qt
    # nearly every target links Qt. qinta ships as a folder: the Qt DLLs and the VC++
    # runtime DLLs sit beside qinta.exe (windeployqt --compiler-runtime in CI), so it
    # still needs no installer. The per-target DLL overrides below predate this line
    # and now restate the default.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
elseif(WIN32)
    # MinGW (the windows-mingw CI job, 2026-10-07) never enters the block above, and without
    # NOMINMAX <windows.h> defines min and max as macros in every file that includes it.
    add_compile_definitions(NOMINMAX)
endif()

# MD4C markdown parser
include(FetchContent)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG 729e6b8b320caa96328968ab27d7db2235e4fb47  # release-0.5.2, pinned by commit
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(md4c)

# Qt is found HERE rather than only at the bottom of this file because the seam
# selection immediately below now depends on it, and the test targets that consume
# the selection are declared hundreds of lines above the old find_package call.
# find_package caches, so the later call is satisfied from here and neither is
# redundant.
#
# Widgets is OPTIONAL here and REQUIRED off Windows in the file-dialog seam below,
# which looks contradictory and is the pattern this file already uses for Svg and
# Network: asking for a component in the COMPONENTS list makes Qt6_FOUND false when
# it is missing, which would silently delete every target in the Qt6_FOUND block
# rather than reporting the one thing that is absent. So it is asked for optionally
# and then checked where it is needed, with a message naming it.
find_package(Qt6 QUIET COMPONENTS Core Gui OPTIONAL_COMPONENTS Widgets PrintSupport)
# PrintSupport since 2026-10-06: src/print_qt.cpp prints through QPrinter and QPrintDialog. It
# ships in qtbase on every host this tree builds on (the Windows kit, Debian's qt6-base-dev,
# Homebrew's qtbase), so it adds no install step; qinta checks for it by name below.

# The platform path seam. Its POSIX half is GONE: src/platform_paths_posix.cpp
# was replaced by src/platform_paths_qt.cpp, which is written to be correct on all
# three platforms. src/platform_paths_win32.cpp collapsed into it on 2026-10-07, after
# the Direct2D stack left, so every platform builds the same file: one stack everywhere.
#
# Nine of the eleven functions have no platform branch in the Qt file. The two that
# do are named there: executableDirectory, because every Qt answer needs a
# QCoreApplication instance and the callers run before one exists, and
# documentPathsEqual, because Qt has no per-platform path-identity rule.
# revealInFileManager keeps a Windows branch as well, so Explorer still selects the file.
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt 6 (Core and Gui) is required: the platform path, keyboard, clipboard, process and "
        "image seams all resolve to their Qt implementations, on every platform.")
endif()
set(PLATFORM_PATHS_SOURCE src/platform_paths_qt.cpp)
set(PLATFORM_PATHS_BACKEND_QT TRUE)

# THE SEAM'S LIBRARIES TRAVEL WITH THE SEAM, and this variable exists because they
# did not. Seventeen targets list ${PLATFORM_PATHS_SOURCE}, and twelve of them
# carried a hand-written `if(WIN32) target_link_libraries(x PRIVATE shell32)` block
# -- shell32 being there for SHGetFolderPathW in the old Windows half of this seam.
# The Qt half needs Qt6::Core and Qt6::Gui the same way, and
# adding it without this variable built ONE target (platform_paths_tests, the only
# one edited by hand) and broke the other sixteen at `QByteArray: No such file or
# directory` -- 15 of 25 ctest entries Not Run, which is a far louder failure than
# the previous file, needing no include path at all, could ever have produced.
#
# So the dependency is stated once, beside the file selection it belongs to. A new
# target that lists the source and forgets the libraries still breaks, but it breaks
# the same way for everyone rather than per-platform, and the fix is one token.
# shell32 stays on Windows for revealInFileManager's ShellExecuteW.
set(PLATFORM_PATHS_LIBS Qt6::Core Qt6::Gui)
if(WIN32)
    list(APPEND PLATFORM_PATHS_LIBS shell32)
endif()

# The modifier-key seam (include/keyboard.h), selected the same way and for the same
# reason. Note the second file is _qt and not _posix: there is no POSIX answer to
# "is Shift held", because that is a question for a window system rather than for an
# operating system. This is the first seam whose non-Windows half is Qt, which is
# what the target stack being Qt 6 actually looks like at the file level.
#
# GetKeyState did NOT go into win32_api.h, whose preamble reserves it for calls that
# become honest no-ops or failures off Windows. This one has a real counterpart in
# QGuiApplication::keyboardModifiers(), so it belongs with the seams that have two
# working implementations, not with the ones that admit to having one. The Windows half
# (GetKeyState) was deleted on 2026-10-07; Qt's answer is the only one on every platform.
set(PLATFORM_KEYBOARD_SOURCE src/keyboard_qt.cpp)

# The image seam (include/image_api.h): remote fetch, raster decode, SVG rasterization.
# Selected the same way, and the reason it exists is narrower than the others. This is
# the seam that removed render.cpp's three UNGUARDED Windows includes -- urlmon.h,
# shlwapi.h and d2d1_3.h. app.h already puts d2d1.h, dwrite.h and wincodec.h behind
# `#ifdef _WIN32`, so every other native mention in that file is an ordinary error;
# those three were FATAL, which meant a 3,498-line file reported `errors=1` and had
# never been measured at all.
#
# The Qt half needs Svg and Network on top of Core and Gui, which is the first time
# this port has needed a Qt module beyond the two.
# One implementation since 2026-10-07: src/image_win32.cpp (WIC) left with the Direct2D stack.
set(PLATFORM_IMAGE_SOURCE src/image_qt.cpp)

# The file-picker seam (include/file_dialog.h): the Open and Save dialogs. Selected
# like the three above, and it is the FIRST seam in this port whose non-Windows half
# needs Qt6::Widgets.
#
# WIDGETS IS UNAVOIDABLE, not a convenience. There is no QtGui-only file dialog:
# QFileDialog is a QWidget, and the only other Qt answer is Qt Quick Dialogs, which
# would pull in Qml and Quick to replace a call that returns a string. So the module
# is the price of a real second implementation instead of a stub that returns "the
# user cancelled" forever -- which is precisely the failure win32_types.h's preamble
# describes, and the reason GetOpenFileNameW did not go into win32_api.h.
#
# UNLIKE Svg AND Network, THIS ONE IS FATAL OFF WINDOWS. Those two gate one optional
# target each, so a partial Qt install loses a test and says so. This seam has no
# other non-Windows half at all, so a build without Widgets would fail at link with
# undefined platform::requestSavePath -- a worse error, further from the cause.
# One implementation since 2026-10-07: src/file_dialog_win32.cpp (comdlg32) left with the
# Direct2D stack, so Widgets is now required on every platform.
if(NOT Qt6Widgets_FOUND)
    message(FATAL_ERROR
        "Qt 6 Widgets is required: src/file_dialog_qt.cpp is the only "
        "implementation of the include/file_dialog.h seam, and QFileDialog "
        "lives in QtWidgets. On Debian and Ubuntu the package is qt6-base-dev, which "
        "also provides the Core and Gui components already required above.")
endif()
set(PLATFORM_FILE_DIALOG_SOURCE src/file_dialog_qt.cpp)
set(PLATFORM_FILE_DIALOG_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)

# The clipboard seam (include/clipboard.h): the system clipboard, text AND image,
# selected like the four above.
#
# QtGui IS ENOUGH HERE, and the contrast with the seam directly above is the point.
# QClipboard hangs off QGuiApplication, so this seam needs no module the tree did not
# already require. Widgets was the price of a file dialog specifically, not the price
# of having a real second implementation in general -- and reading it as the latter
# would make every future seam look more expensive than it is.
#
# THE IMAGE HALF JOINED THIS SEAM ON 2026-09-19, and the sentence that kept it out is
# quoted rather than deleted because one of its two clauses was WRONG and the other was
# RIGHT. It read: "The IMAGE half of the clipboard is NOT part of this seam.
# src/export_docx.cpp keeps CF_DIB, a registered L\"PNG\" format and a CF_BITMAP read,
# because none of the three has a QClipboard shape that is a translation rather than a
# rewrite, and that file cannot compile off Windows for an unrelated reason anyway. So a
# build off Windows gets the text half and no image half, which is honest: it is what
# exists."
#
# WRONG: two of the three ARE translations -- CF_DIB becomes a QImage on a QMimeData and
# the registered L"PNG" format becomes an "image/png" entry on the same QMimeData. The
# CF_BITMAP READ is the one genuine rewrite, and that rewrite is what the slice was FOR:
# GetDIBits replaced an IWICBitmap, which is how a 52-line WIC encoder left
# src/export_docx.cpp.
#
# THE "RIGHT" CLAUSE WAS ALSO WRONG, and it is worth reading why before trusting the next
# comment of this shape. It read: "RIGHT: that file still cannot compile off Windows, and
# nothing here claims otherwise. objbase.h and the OPC package are why, and they are
# untouched. What changed is that the clipboard is no longer one of the reasons."
#
# It was false the moment it was written, BY THE SLICE THAT WROTE IT. Moving the CF_BITMAP
# read behind the seam removed the file's last COM caller, so the objbase.h it named as the
# reason had become an unused include in the same commit. The OPC half never needed COM at
# all -- the zip and the WordprocessingML are hand-written through <fstream> and
# std::string. Measured 2026-09-20: with the three dead includes gone,
# src/export_docx.cpp censuses exit 0, fatal 0, errors 0, and it is on
# tests/portable-sources.txt so CONTROL 5 keeps it that way.
#
# The lesson is narrow and reusable: this comment correctly identified one of its
# predecessor's two clauses as wrong, then carried the other one forward UNMEASURED because
# it looked untouched. A clause surviving a correction is not thereby verified -- and the
# remaining reason a file is non-portable is exactly the claim a port is most likely to
# have just invalidated.
#
# So a build off Windows now gets both halves of the clipboard, and src/export_docx.cpp is
# no longer a Windows-only file at all.
# src/clipboard_win32.cpp (CF_UNICODETEXT, CF_DIB, the registered PNG format and the
# CF_BITMAP read) was deleted on 2026-10-07: QClipboard is the only half on every platform.
set(PLATFORM_CLIPBOARD_SOURCE src/clipboard_qt.cpp)
set(PLATFORM_CLIPBOARD_LIBS Qt6::Core Qt6::Gui)

# The PROCESS seam -- the sixth -- added 2026-09-20 for src/pandoc.cpp, which was the
# last file in the census whose Win32 surface was a set of CALLS rather than a fatal
# include. include/platform_process.h states why this is a seam at all rather than an
# entry in win32_api.h: a member of THAT header is a no-op or an honest failure off
# Windows ("NOTHING HERE PRETENDS TO WORK", win32_api.h:17), and a pandoc export has to
# genuinely run on Linux. win32_types.h is no home either -- it refuses functions.
#
# THE QT ARM NEEDS Qt6::Core AND NOTHING MORE, which is worth stating because every
# other seam here needed Gui or Widgets as well. QProcess and QCoreApplication are both
# Core classes, so the one seam that spawns a program is the cheapest of the six to link
# -- there is no GUI in running a child process.
#
# The Windows arm names no library for the same reason ${PLATFORM_CLIPBOARD_LIBS} is
# empty: CreateProcessW, WaitForSingleObject, TerminateProcess, GetExitCodeProcess and
# GetCurrentProcessId are all kernel32, which every Windows link gets by default. The
# variable exists anyway so a target can list it unconditionally.
# src/platform_process_win32.cpp (CreateProcessW over include/win32_command_line.h's
# quoter) was deleted on 2026-10-07: QProcess is the only half on every platform.
set(PLATFORM_PROCESS_SOURCE src/platform_process_qt.cpp)
set(PLATFORM_PROCESS_LIBS Qt6::Core)

# The TINTA_LEGACY_D2D option and its ${LEGACY_D2D_SOURCES} stood here until 2026-10-07,
# when the Direct2D/DirectWrite backends they selected were deleted.

# ── The embedded app icon, generated for BOTH backends ────────────────────────────────
#
# resources/tinta.ico becomes a C++ byte array that the Direct2D and Qt builds read
# identically, decoded through tinta::decodeIconBytesToBitmap. include/app_icon_data.h
# carries the reasoning, including why this is NOT a Qt .qrc: a Qt-only mechanism would
# have left the Direct2D side on LoadImageW and kept an #ifdef at the call site, which is
# the thing the seam exists to remove.
#
# CONFIGURE time, not build time, and the dependency is declared rather than assumed:
# CMAKE_CONFIGURE_DEPENDS re-runs configure when the .ico changes, so editing the artwork
# regenerates the array. Without that line a new icon would be silently ignored until
# someone deleted the build directory -- a stale asset that nothing reports.
set(TINTA_ICON_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/resources/tinta.ico)
set(TINTA_ICON_GENERATED ${CMAKE_BINARY_DIR}/generated/app_icon_data.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TINTA_ICON_SOURCE})
file(READ ${TINTA_ICON_SOURCE} TINTA_ICON_HEX HEX)
string(LENGTH "${TINTA_ICON_HEX}" TINTA_ICON_HEX_LEN)
if(TINTA_ICON_HEX_LEN LESS 64)
    # A truncated or missing asset would otherwise generate a valid, tiny array and the
    # icon would simply never appear -- a silent failure with no diagnostic anywhere.
    message(FATAL_ERROR "resources/tinta.ico read as only ${TINTA_ICON_HEX_LEN} hex chars")
endif()
string(REGEX REPLACE "(..)" "0x\\1," TINTA_ICON_BYTES "${TINTA_ICON_HEX}")
# THE TWELVE GROUPS ARE WRITTEN OUT AND THAT IS NOT A STYLE CHOICE. This line first read
# `((0x..,){12})`, which CMake's regex engine does not support -- it has no {n} repetition
# operator -- and an unsupported quantifier here does not error, it simply matches nothing.
# The replacement became a no-op, the generated file was still perfectly valid C++, and the
# only symptom was ONE 182,459-CHARACTER LINE. Measured with awk rather than noticed by
# reading, because nothing downstream complains: MSVC compiles it, git stores it, and the
# file is gitignored so no diff ever shows it.
string(REGEX REPLACE
    "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n    "
    TINTA_ICON_BYTES "${TINTA_ICON_BYTES}")
file(WRITE ${TINTA_ICON_GENERATED}
"// GENERATED FILE -- DO NOT EDIT AND DO NOT COMMIT.\n"
"// Written by CMakeLists.txt from resources/tinta.ico. See include/app_icon_data.h.\n"
"#include \"app_icon_data.h\"\n"
"\n"
"namespace tinta {\n"
"\n"
"const unsigned char kAppIconIco[] = {\n    ${TINTA_ICON_BYTES}\n};\n"
"const size_t kAppIconIcoSize = sizeof(kAppIconIco);\n"
"\n"
"}  // namespace tinta\n")

# The `tinta` target -- the Direct2D/DirectWrite Windows app -- and its ${SOURCES},
# ${HEADERS} and ${RESOURCES} lists stood here until 2026-10-07, when the Direct2D stack was
# retired. qinta is the application on every platform; its sources are ${QINTA_APP_SOURCES}.

# --- THE QT APPLICATION: the target the whole port is aimed at ------------------
#
# One stack on every platform means this target eventually REPLACES `tinta` above
# rather than joining it, which is why src/render_d2d.cpp, src/text_d2d.cpp and
# src/platform_paths_win32.cpp were deleted rather than ported (2026-10-07), and why
# src/settings_win32.cpp stays as a Windows-only-by-design file for /register. It is built on WINDOWS TOO wherever Qt is found:
# a Qt target that only exists off Windows would let the two stacks drift, and
# drifting is the thing the goal rules out.
#
# WHAT IT IS TODAY: it renders a document. It parses markdown and lays it out through
# the portable render and text seams, which is the thing this target existed to reach.
# The sentence here used to read "a shell -- it opens a window, clears to the theme
# background and draws one line of text ... It does NOT render a document", and that
# is corrected rather than deleted because the shape of what changed is the useful
# part: the blocker was never one file, and it was never a compile problem at the end.
#
# THE SOURCE LIST IS A MEASURED LINK CLOSURE, and it has now been measured twice with
# two different answers, which is the whole lesson of this target.
#
#   FIRST READING (2026-09-16, the shell): a scratch probe built the 39 portable
#   objects into an archive, linked a Qt main() against it and read the link map.
#   Constructing an App and painting through the seam pulls exactly six -- config_dir,
#   markdown, platform_paths_qt, themes, render_qt, text_qt. markdown.cpp was the
#   surprise and the map named the reason: App holds a qmd::MarkdownParser BY VALUE,
#   so constructing an App references its constructor, which references md_parse.
#   That is the third time this tree learned that fact from a linker rather than from
#   reading (see selection_tests below, and tests/portable-sources.txt).
#
#   SECOND READING (rendering a real document): a FIXPOINT closure, because depth-1
#   and depth-2 are both wrong answers to this question. Adding render.cpp leaves 23
#   undefined symbols; resolving those leaves 41, the 21 new ones all
#   mermaidext::detail::build* from eleven further files -- resolving a symbol ADDS
#   symbols, so any fixed-depth reading is a lower bound wearing the clothes of an
#   answer. The fixpoint converged 23 -> 41 -> 20 in three rounds, and classified the
#   20: fourteen live in installed Qt shared libraries (checked with nm -D, not
#   guessed) and are satisfied by the two library lines below, and SIX were genuinely
#   blocked -- mathParse, mathBoxWidth, mathBoxHeight, mathBoxBaseline,
#   mathExpandLine, mathBoxRetain, all owned by src/math_render.cpp.
#
# A PREDICTION OF MINE WAS REFUTED HERE AND THE SOURCE LIST IS THE SHAPE OF THE
# REFUTATION. I expected a staircase: land the nineteen already-compiling files
# first, deal with math later. Two arms measured it and the answer is no --
# ensureLayoutComplete(App&) is DEFINED in render.cpp and called from search.cpp:158,
# so the nineteen and render.cpp are one atomic lump. There is no smaller first step
# than all of this at once plus an answer on math.
#
# src/math_stub.cpp WAS THAT ANSWER, AND IT IS NOW GONE -- the temporary it always said
# it was. It supplied the six symbols so this target linked while src/math_render.cpp was
# Windows-only, and equations rendered as their own LaTeX source through the fallback
# include/math_render.h documents and render.cpp implements at :439, :519 and :895. Its
# deletion condition was "when src/math_render.cpp compiles off Windows", and that is
# what this slice did: the file measures through app.textFactory and draws through a
# tinta::RenderTarget, so the six symbols are the REAL ones and equations are typeset.
# The fallback branches stay -- they are the contract for unparseable input, which is a
# different thing from an unported backend.
#
# So md4c is on the link line for a target that now genuinely parses. The Svg and
# Network lines are not optional decoration either: they carry fourteen of the twenty
# residual symbols, from image_qt.cpp's QSvgRenderer and QNetworkAccessManager.

# THE QT APP'S SOURCE CLOSURE, IN A VARIABLE BECAUSE TWO TARGETS MUST SHARE IT.
#
# qinta below is the application; qt_entry_tests far down in the test section is the
# only thing that PAINTS it and reads pixels back. Those two have to link the same
# files or the test stops being a test of the app -- and the comment on that target has
# claimed "the same measured closure as qinta, minus main_qt.cpp" since it was
# written, on nothing but my promise to keep two hand-maintained lists in step.
#
# This makes the claim structural instead. The lists cannot drift, because there is one
# list. That matters more now than it did when both were short: the closure is 28 files
# and a file added to the app but not the test would produce a green test over code the
# app links and the test does not.
#
# Deliberately at unconditional scope rather than inside the Qt guard below, so the
# test section can read it without depending on the order of two independent if()s.
set(QINTA_APP_SOURCES
    ${TINTA_ICON_GENERATED}
    src/qt_window.cpp
    src/qt_init.cpp
    src/render_qt.cpp
    src/text_qt.cpp
    src/themes.cpp
    src/markdown.cpp
    src/config_dir.cpp
    ${PLATFORM_PATHS_SOURCE}
    # --- the fixpoint closure's nineteen, plus render.cpp and the math stub ---
    # Every line below is a symbol the closure demanded, not a file that looked
    # relevant. The mermaid* run is eleven files and they arrive as a group
    # because mermaidext::detail::build* is how mermaid.cpp reaches each diagram
    # kind -- dropping any one of them is a link error, not a missing feature.
    src/render.cpp
    # The document paint pass. NOT part of the fixpoint closure's twenty, because it
    # did not exist when that closure was measured -- it is the code this slice cut out
    # of src/main_d2d.cpp so that something other than a Win32 WM_PAINT could call it.
    src/paint_document.cpp
    # The scrollbars. Not a QScrollBar -- include/scrollbar.h explains why at length, but
    # the build-level version is short: this is the SAME file the `tinta` target links, so
    # both stacks draw the identical thumb and neither can restyle it alone.
    src/scrollbar.cpp
    # The whole frame: the SAME file tinta's render() calls, so the two windows draw one frame.
    src/render_frame.cpp
    # THE REAL MATHS BACKEND, AND src/math_stub.cpp IS GONE. That file supplied six
    # symbols so this target could link while src/math_render.cpp was Windows-only, and
    # its own header said "DELETING THIS FILE IS THE GOAL. It goes when math_render.cpp
    # compiles off Windows." It does now, so the file is deleted rather than left
    # unreferenced -- an unreferenced stub is a second implementation of six symbols
    # waiting for someone to link it by accident.
    #
    # Three files, not one, and the LINKER named the other two rather than a reading of
    # the include graph: math_render.cpp calls parseLatex out of math_parser.cpp, which
    # reaches expandMacros in math_macros.cpp. Both were already portable -- the
    # math_parser_tests target above sits OUTSIDE the WIN32 guard and links no platform
    # library -- so this is three files added and no new portability question asked.
    #
    # THE PRODUCT CHANGE, stated because it is visible and was the point: equations are
    # TYPESET here now. They used to render as their own LaTeX source in a code block
    # through the fallback include/math_render.h documents, which is what a reader saw
    # in every qinta build before this one.
    src/math_render.cpp
    src/math_parser.cpp
    src/math_macros.cpp
    # The command line's two files, and the LINKER is what added them rather than a
    # reading of the include graph. src/main_qt.cpp's one new call --
    # loadDocumentFromPath, in file_utils.cpp -- came back `undefined reference` from
    # main_qt.cpp:(.text.startup), and file_utils.cpp in turn wants parseDocument out of
    # document.cpp, which nothing on this stack had ever needed: the test parsed with
    # app.parser.parse() directly and the window never loaded anything. Two files for one
    # call, and neither was predictable from the fact that both were already portable --
    # being in tests/portable-sources.txt says a file COMPILES off Windows, never that
    # anything LINKS it, which is the whole reason README.md's bases B and C are separate
    # rows.
    src/file_utils.cpp
    src/document.cpp
    # THE LAUNCH SEQUENCE'S THREE FILES, and the LINKER added these too -- the paragraph
    # above turns out to describe a repeating shape rather than one incident. src/main_qt.cpp
    # now parses the command line, loads the four geometry fields, and decides where the
    # window goes, so it needs all three, and all three came back as `unresolved external`
    # in one link rather than from reading the includes.
    #
    # Worth noting WHICH three, because it is the argument for the seams existing: every one
    # of them is already linked by the `tinta` target through ${SOURCES}. They are not new
    # code with a new consumer, they are the SAME code with a second consumer, which is the
    # thing that was impossible while the logic sat inside WinMain. window_placement.cpp is
    # listed by the placement test target as well, and launch_options.cpp by its own -- so
    # three targets link each, and a behaviour change has to satisfy all of them.
    src/launch_options.cpp
    src/window_placement.cpp
    # The drag ghost's portable half, and the Qt arm that draws it. Same argument as the three
    # files above: tab_ghost.cpp is already linked by `tinta` through ${SOURCES}, so this is the
    # same code with a second consumer. tab_ghost_qt.cpp is Qt-only and therefore NOT in
    # ${SOURCES}; it is the frameless translucent widget standing where UpdateLayeredWindow stands
    # on the other arm, and it draws through tabGhostCovers rather than through a QPainterPath
    # clip that would merely agree with it.
    src/tab_ghost.cpp
    src/tab_ghost_qt.cpp
    # Zen mode's Qt arm. Qt-only, so here and NOT in ${SOURCES}. It reaches the same monitor the
    # Windows arm's MONITOR_DEFAULTTONEAREST would have picked by calling window_placement.cpp's
    # zenFullscreenRect rather than QWidget::screen()->geometry() -- those two disagree for a window
    # restored onto an unplugged monitor, which is the case window_placement.cpp exists for.
    src/zen_mode_qt.cpp
    # The Qt arm's two launch steps -- Qt's screen list as a monitor list, and parsed options
    # plus loaded settings as a PlacementRequest. Qt-only, so it is here and NOT in ${SOURCES};
    # it exists at all so tests/qt_entry_tests.cpp can call the same two steps main() calls
    # instead of a copy of them.
    src/qt_launch.cpp
    # settings.cpp for loadSettings(). Six fields are read off the result -- four for the
    # placement, two for the keymap; the other twenty-four reach Windows-only consumers and
    # are deliberately not applied, which
    # src/main_qt.cpp's header comment states at the site rather than leaving to inference.
    src/settings.cpp
    # And keymap.cpp, which the linker demanded on the NEXT attempt: loadSettings() restores
    # key bindings through keyProfileIndexById/keyIniName whether or not this entry point
    # cares about key bindings, because a settings loader reads the whole file. THREE
    # successive links to reach the closure of one call, which is the same lesson the
    # file_utils/document paragraph above records -- so it is now recorded twice on purpose:
    # "it compiles off Windows" and "it is already in ${SOURCES}" both say nothing about what
    # a new consumer drags onto a DIFFERENT link line, and the only reliable instrument is
    # the link itself.
    src/keymap.cpp
    # The scroll clamp, and the file dialog Ctrl+O opens. src/scroll.cpp is portable and
    # holds one function, the clamp resizeEvent calls; the scroll numbers themselves are
    # src/input.cpp's, which both stacks route to (include/scroll.h has the history).
    #
    # src/file_dialog_qt.cpp is named DIRECTLY rather than through
    # ${PLATFORM_FILE_DIALOG_SOURCE}, and that is the port's stated goal showing up in
    # the build for the first time: on Windows that variable resolves to
    # src/file_dialog_win32.cpp, and qinta must not take the Win32 half of a seam it
    # has a Qt half for. The legacy `tinta` target still links the Win32 half. No target
    # links both, so there is no duplicate symbol.
    src/scroll.cpp
    src/file_dialog_qt.cpp
    src/image_qt.cpp
    src/inline_style.cpp
    src/mermaid.cpp
    src/mermaid_block.cpp
    src/mermaid_c4.cpp
    src/mermaid_charts.cpp
    src/mermaid_charts2.cpp
    src/mermaid_ext.cpp
    src/mermaid_gantt.cpp
    src/mermaid_git.cpp
    src/mermaid_grid.cpp
    src/mermaid_narrative.cpp
    src/mermaid_pie.cpp
    src/mermaid_seq.cpp
    src/mermaid_uml.cpp
    src/search.cpp
    src/syntax.cpp
    src/text_utils.cpp
    src/text_utils_app.cpp
    src/text_formats_app.cpp
    # THE KEY ROUTER'S CLOSURE. src/qt_window.cpp's keyPressEvent calls handleKeyDown and
    # handleCharInput, so input.cpp has a caller on this stack and everything it reaches
    # comes with it: the 18 the 2026-09-24 Windows link measured, the three portable files
    # the Linux fixpoint already had as base (context_menu, i18n, selection) plus
    # i18n_app, and three OS seams through their variables -- see the IF YOU DO ADD A SEAM
    # ARM rule below for why never as a filename.
    src/qt_key_map.cpp
    src/input.cpp
    src/editor.cpp
    src/drafts.cpp
    src/editrail.cpp
    src/startpage.cpp
    src/tabs.cpp
    src/tableedit.cpp
    src/annotations.cpp
    src/overlays.cpp
    src/pandoc.cpp
    src/signals.cpp
    src/utils.cpp
    src/export_docx.cpp
    src/export_html.cpp
    # The print code, PORTABLE since 2026-10-06: preview, pagination, PDF export and the
    # link peek, with the Qt arms of its two backend hooks (src/print_qt.cpp: the paper
    # locale, and printDocument through QPrinter and QPrintDialog since 2026-10-06) and the Qt arm of the PDF seam. Until then
    # this list carried print_qt.cpp alone, and it defined all seven print symbols as
    # refusals.
    src/print.cpp
    src/print_qt.cpp
    src/pdf_export_qt.cpp
    # Ctrl+Shift+N's new window and the note-in-its-own-window relaunch, the Qt arms of what
    # src/quicknote_win32.cpp does for tinta (2026-10-06).
    src/window_launch_qt.cpp
    # The single-instance hand-off: a second launch hands its document to the running window
    # over QLocalSocket, WinMain's FindWindowW + WM_COPYDATA (2026-10-06).
    src/instance_channel_qt.cpp
    src/settings_app.cpp
    src/theme_apply_app.cpp
    src/theme_qt.cpp
    src/context_menu.cpp
    src/i18n.cpp
    src/i18n_app.cpp
    src/selection.cpp
    ${PLATFORM_CLIPBOARD_SOURCE}
    ${PLATFORM_KEYBOARD_SOURCE}
    ${PLATFORM_PROCESS_SOURCE}
)

# WHY THIS LIST WAS 47 OBJECTS AND NOT THE MANIFEST'S 68, MEASURED 2026-09-22.
#
# SUPERSEDED AGAIN 2026-09-25, BY THE KEY ROUTER: the block headed THE KEY ROUTER'S CLOSURE
# above is the 21 plus what they reach, added with their caller -- which is the condition the
# "nothing calls them" paragraph below set, now met. Everything from here down is the record
# of why they waited.
#
# COMPILES OFF WINDOWS AND LINKS INTO qinta ARE TWO DIFFERENT PROPERTIES, and
# tests/portable-sources.txt only ever claimed the first. Reading it as the second
# is what let the 2026-09-21 hand-off size the remaining port at ONE file
# (src/print.cpp) when the gap is TWENTY-ONE. This comment exists so the next
# reader gets the measurement instead of re-deriving it from a manifest that
# cannot see a link.
#
# 68 manifest entries, 47 linked here, so 21 compile portably and are not linked.
# Adding all 21 and reading the linker -- not the manifest -- says: 7 close and
# 14 do not.
#
# SUPERSEDED 2026-09-24, AND THE REST OF THIS BLOCK IS KEPT AS THE RECORD IT WAS. The same
# fixpoint (qinta's own Linux link line, every compile-only and manifest-only object as a
# candidate, drop what fails to resolve, repeat) now reads 26 candidates, round 1
# link_exit=0 drop=0, 26 survivors: ALL 14 CLOSE. What closed them was 14 symbols in three
# files, not the two root blockers below -- print.cpp's seven (now defined off Windows by
# src/print_qt.cpp, every one a refusal), d2d_init.cpp's three (src/theme_apply_app.cpp and
# src/theme_qt.cpp) and settings_app.cpp's four (compiled off Windows now). tabs.cpp is a
# candidate in that run and survives it, so it withholds nothing. The control: the same run with
# print_qt excluded drops input, editor and export_html in round 1 and all 14 by round 3.
# So the reason none of them is in this list is now the one below, "nothing calls them",
# and nothing else.
#
# THERE ARE EXACTLY TWO ROOT BLOCKERS. Everything else in the 14 is transitive
# through them. "and both are files no non-Windows compiler can read at all" stood
# in that sentence until 2026-09-23 and was then true of print.cpp only, until
# 2026-10-06, when print.cpp joined the manifest:
#
#   src/tabs.cpp   IN the manifest's compile-only section since 2026-09-23, NOT
#                  linked into qinta. ~~Stops at `#include <shellapi.h>`~~ --
#                  since 2026-09-23 it has no fatal include, and since the same
#                  day's tear-off pass it COMPILES off Windows: errors 0. The 40
#                  error lines it read in between were window management
#                  (FindWindowExW, WindowFromPoint, SetForegroundWindow), IPC
#                  (WM_COPYDATA, COPYDATASTRUCT), chrome (IsZoomed, SetWindowPos)
#                  and the ghost window's three calls. The search and hand-off
#                  moved to src/tab_tearoff_win32.cpp; the rest sit under site
#                  guards that say what is missing off Windows (no detach, no pin,
#                  the maximise glyph reads not-maximised). So it is still a ROOT
#                  BLOCKER, but for a LINK reason now, like the files it blocks.
#                  Being unlinked is NOT why it was off the manifest -- the manifest
#                  claims compile only -- and it joined once that was seen:
#                  it is not linked into qinta, so it withholds 7 symbols (tabsInit, tabOpenPath, tabCloseIndex,
#                  tabOpenQuickNote, tabBecomeStartPage, tabBulkCloseStep,
#                  tabStripBackground) and so blocks drafts.cpp, editor.cpp,
#                  editrail.cpp, startpage.cpp.
#   src/print.cpp  [SUPERSEDED 2026-10-06: in the manifest and linked into qinta; its
#                  Windows half is src/print_win32.cpp.] not in the manifest. Stops at
#                  d2d1.h, reached through
#                  include/xps_page_emitter.h:24. ~~Withholds openPrintPreview and
#                  exportPdfFile, blocking editor.cpp and export_html.cpp.~~ -- print.cpp
#                  itself is unchanged and still Windows-only, but it blocks nothing
#                  since 2026-09-24: its seven called symbols have a refusing Qt arm in
#                  src/print_qt.cpp.
#
# src/input.cpp IS NOT A ROOT BLOCKER AND CALLING IT ONE IS THE MISTAKE THIS
# COMMENT IS HERE TO STOP. It is a manifest entry and compiles off Windows with
# zero errors; it ~~is absent from this list only because it needs six symbols that
# only src/print.cpp defines. The same is true of every other name in the 14.~~ was absent
# for six symbols only src/print.cpp defined until 2026-09-24; now it is absent only
# because nothing calls it, like every other name in the 14.
#
# CONSEQUENCE FOR THE FLAGS (four of the ten honoured since 2026-10-06): all ten refusals in src/main_qt.cpp ~~are behind these
# two files~~ were behind these two files; since 2026-09-24 they are behind the missing
# caller, and the three export flags would reach print_qt.cpp's refusal for PDF (since 2026-10-06
# they would reach a real QPdfWriter export instead; main_qt.cpp still refuses the flags). That includes --exportpdf, --exporthtml and --exportdocx, which look
# independent and are not -- export_html.cpp calls exportPdfFile, and
# export_docx.cpp calls buildDiagramPrims in export_html.cpp.
#
# WHY THE 7 THAT WOULD CLOSE ARE NOT ADDED (all 21 since 2026-09-24): nothing calls them. qinta links clean
# without them, so each is an object with no caller and no test, and this tree's
# own rule is not to add code serving a caller that does not exist. When the
# router lands they go in with something that exercises them. -- It landed 2026-09-25:
# they are in, and tests/qt_entry_tests.cpp drives keys through them.
#
# IF YOU DO ADD A SEAM ARM HERE, add it as ${PLATFORM_*_SOURCE} and never as a
# _qt.cpp filename: each has a win32 counterpart, so a filename entry links both
# arms on Windows and raises LNK2005 on that host only. ${PLATFORM_PATHS_SOURCE}
# above is the pattern. src/file_dialog_qt.cpp and src/image_qt.cpp are older
# hardcoded literals and are deliberately left alone.

# qinta is the app on Windows too, so a Windows configure without Qt has nothing to build.
# It used to succeed anyway and just leave qinta out, which read as a working build until
# `--target qinta` found no such target. Off Windows the seam selection above already stops.
if(WIN32 AND NOT (Qt6_FOUND AND Qt6Widgets_FOUND))
    message(FATAL_ERROR
        "Qt 6 (Core, Gui and Widgets) is required on Windows: qinta is the Windows app. Point "
        "CMAKE_PREFIX_PATH at a Qt 6 MSVC kit, e.g. -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64")
endif()

if(Qt6_FOUND AND Qt6Widgets_FOUND AND NOT Qt6PrintSupport_FOUND)
    message(FATAL_ERROR
        "Qt 6 PrintSupport is required for qinta: src/print_qt.cpp prints through QPrinter. It is "
        "part of qtbase, so a kit that has Widgets and not PrintSupport is an incomplete install.")
endif()

if(Qt6_FOUND AND Qt6Widgets_FOUND)
    add_executable(qinta WIN32
        src/main_qt.cpp
        ${QINTA_APP_SOURCES}
    )
    target_include_directories(qinta PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(qinta PRIVATE md4c
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Qt6::PrintSupport)
    # OUTSIDE the if(WIN32) below, deliberately: the version numbers are not a platform
    # fact, and every UNICODE definition in this file is guarded, so the tempting place to
    # add these is the one place they would silently not apply off Windows.
    target_compile_definitions(qinta PRIVATE ${TINTA_VERSION_DEFINITIONS})
    # TINTA_QT_UI: "this target's window is a QWidget", which is a DIFFERENT QUESTION from
    # _WIN32, the same split TINTA_D2D_BACKEND makes for the renderer. On Windows qinta is
    # a Win32 OS with a Qt window, and `#ifdef _WIN32` could only answer the first: linking
    # the router's files into qinta here read `LNK1120: 50 unresolved externals`, and the
    # window-code share of them was zen_mode_win32, tab_ghost_win32, tab_tearoff_win32,
    # quicknote_win32 and the IME -- Win32 window arms the callers picked because the OS
    # was Windows. So every guard around WINDOW code reads
    # `defined(_WIN32) && !defined(TINTA_QT_UI)`, and every guard about the OS (file
    # times, process ids, the date format, the VK constants) stays `_WIN32`.
    #
    # OPT-IN ON THE QT TARGETS, deliberately the opposite polarity to TINTA_D2D_BACKEND: a
    # target that forgets it keeps today's Win32 arms, so `tinta` cannot change silently,
    # and a Qt target that forgets it fails its link loudly with the list above. Defined
    # off Windows too, where it changes nothing (_WIN32 is already false) but keeps the
    # definition a fact about the target rather than the host. qt_entry_tests carries it
    # as well and asserts it arrived: requestQuit() must return false there on both hosts.
    target_compile_definitions(qinta PRIVATE TINTA_QT_UI)
    if(WIN32)
        # UNICODE/_UNICODE because the whole tree assumes the wide Win32 API, and
        # shell32 because ${PLATFORM_PATHS_SOURCE} is still the win32 file here. Both
        # go away at the cutover this target exists to reach: the path seam collapses
        # onto src/platform_paths_qt.cpp, which is written to be correct on all three.
        target_compile_definitions(qinta PRIVATE UNICODE _UNICODE)
        target_link_libraries(qinta PRIVATE shell32)
        # The title bar (slice 13): the Qt window paints applyTheme's caption colours through
        # the same DWM file as tinta. Not a cutover leftover like shell32 -- DWM is the only
        # caption-colour API Windows has, so this stays after the path seam collapses.
        target_sources(qinta PRIVATE src/window_chrome_win32.cpp)
        target_link_libraries(qinta PRIVATE dwmapi)
        # The file association (slice 26, 2026-10-07): /register and the first-run question,
        # in src/settings_win32.cpp, and the icon resource its DefaultIcon (`qinta.exe,0`)
        # names, with the version metadata, in resources/qinta.rc. The registry calls are
        # advapi32 and kernel32, which every Windows link already gets; SHChangeNotify and
        # ShellExecuteW are shell32, linked above.
        target_sources(qinta PRIVATE src/settings_win32.cpp resources/qinta.rc)
        # Dynamic CRT, the same override and the same measured reason as the Qt test
        # targets far below: Qt cannot be linked against the static CRT, and mixing the
        # two debug heaps in one process trips an assertion in Qt's own allocations.
        #
        # Since 2026-10-07 the tree-wide default at the top of this file is the dynamic
        # CRT too, so this restates it; README.md's C runtime section has the decision.
        set_property(TARGET qinta PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    message(STATUS "Qt6 ${Qt6_VERSION} found -- qinta (the Qt shell) enabled")
else()
    message(STATUS "Qt6 with Widgets not found -- qinta skipped")
endif()

if(BUILD_TESTING)
    # --- Platform-independent targets: these build and run on every platform.
    # They link no Windows libraries, so a regression that drags Windows into the
    # portable core breaks them WHEN THEY ARE BUILT OFF WINDOWS. On Windows these
    # targets compile happily either way, so a green run HERE is not evidence of
    # portability -- MSVC accepts them whether or not they still need Windows.
    #
    # Run scripts/probe-portable-linux.sh for the real reading, or
    # `cl /showIncludes` on a single changed file for header topology alone.
    #
    # Every src/ file reachable from the targets in this section must be listed in
    # tests/portable-sources.txt, which the probe compares against what the build
    # actually compiled -- so adding a source here without listing it there fails,
    # and so does quietly dropping one.
    add_executable(keymap_tests tests/keymap_tests.cpp src/keymap.cpp)
    target_include_directories(keymap_tests PRIVATE include)
    add_test(NAME keymap COMMAND keymap_tests)

    # include/geometry.h holds the plain-value colour and rectangle types that
    # replaced D2D1_COLOR_F and friends. Every one of the ~1,929 sites it serves
    # is in a file that is still Windows-only, so without this target the
    # non-Windows build compiles NONE of the header -- its portable half, which
    # is its entire purpose, would have no instrument. Header-only, so there is
    # no src/ file to add to tests/portable-sources.txt; the probe asserts on the
    # test object instead.
    add_executable(geometry_tests tests/geometry_tests.cpp)
    target_include_directories(geometry_tests PRIVATE include)
    add_test(NAME geometry COMMAND geometry_tests)

    # WHETHER A REPAINT REQUEST REACHES ANYTHING, which until now nothing could ask.
    # include/win32_api.h::requestRepaint is the tree's single repaint seam at 277 call
    # sites under src/, and off Windows its entire body was `(void)hwnd;` -- so every
    # one of those requests returned cleanly having scheduled nothing. A no-op and a
    # working repaint are indistinguishable from the caller's side, which is precisely
    # why this needs a target rather than a reading: the state really does mutate, the
    # call really does return, and the screen never changes.
    #
    # Header-only on both sides -- include/repaint_sink.h and the seam itself -- so
    # there is no src/ file for tests/portable-sources.txt and the probe asserts on the
    # test object instead, the same treatment geometry.h and render_api.h get above.
    # Outside if(WIN32) deliberately: the arm this exercises is the non-Windows one, so
    # a Windows-only target would be an instrument pointed away from its subject.
    add_executable(repaint_sink_tests tests/repaint_sink_tests.cpp)
    target_include_directories(repaint_sink_tests PRIVATE include)
    add_test(NAME repaint_sink COMMAND repaint_sink_tests)

    # armTimer and disarmTimer's sink, include/timer_sink.h, on the same argument.
    add_executable(timer_sink_tests tests/timer_sink_tests.cpp)
    target_include_directories(timer_sink_tests PRIVATE include)
    add_test(NAME timer_sink COMMAND timer_sink_tests)

    # WHERE A WINDOW APPEARS, WHICH UNTIL NOW NOTHING COULD ASK. The decision lived in
    # thirty-five lines inside WinMain -- validate the saved rect against the monitors that
    # exist now, clamp it into the nearest work area if it is off all of them (issue #25),
    # then let --pos override or --cascade offset. No test in this tree can call WinMain, so
    # the #25 fix has been correct-by-inspection since it shipped.
    #
    # src/window_placement.cpp is in ${SOURCES}, so the six Windows targets already compile
    # it; this target is what RUNS it, on either platform. Two libraries' worth of nothing:
    # it needs no Qt, no windows.h and no display, because the function's only inputs are
    # the rectangles it is handed.
    add_executable(window_placement_tests
        tests/window_placement_tests.cpp
        src/window_placement.cpp
    )
    target_include_directories(window_placement_tests PRIVATE include)
    add_test(NAME window_placement COMMAND window_placement_tests)

    # WHAT A LAUNCH WAS ASKED FOR, which the two arms disagreed about by omission. Same shape
    # as the target above and same absence of dependencies -- no Qt, no windows.h, no display,
    # because argv is a vector of strings on every platform there is.
    #
    # The case this target exists for is a defect rather than a regression risk: as shipped,
    # src/main_qt.cpp treats argv[1] as a document path, so `tinta --light a.md` opens nothing
    # and reports that it cannot find a file called "--light". The census at the bottom of the
    # test is the other half -- it walks every option spelling WinMain accepts and fails if the
    # shared parse does not know one, so a fourteenth option added to one arm is caught here.
    add_executable(launch_options_tests
        tests/launch_options_tests.cpp
        src/launch_options.cpp
    )
    target_include_directories(launch_options_tests PRIVATE include)
    add_test(NAME launch_options COMMAND launch_options_tests)

    # WHAT THE DRAG GHOST LOOKS LIKE, which until this slice nothing could ask. Same shape as the
    # two targets above -- no Qt, no windows.h, no display -- because a card's width, its corner
    # cut and its offset from the cursor are arithmetic over one float.
    #
    # The assertion worth naming is the corner symmetry. The shipped alpha rebuild has four
    # branches, the left pair measuring from `radius` and the right pair from `width - 1 - radius`,
    # and nothing anywhere asserted those describe the same curve. A one-pixel error in either
    # right-hand branch produces a card with a heavier corner on one side, which looks like a font
    # artefact and never gets filed.
    add_executable(tab_ghost_tests
        tests/tab_ghost_tests.cpp
        src/tab_ghost.cpp
    )
    target_include_directories(tab_ghost_tests PRIVATE include)
    add_test(NAME tab_ghost COMMAND tab_ghost_tests)

    # tab_ghost_win32_tests left with src/tab_ghost_win32.cpp on 2026-10-07; the ghost
    # window's Qt arm is asserted by tab_ghost_qt in the Qt block.

    # The platform path seam, plus config_dir.cpp itself. config_dir.cpp is in
    # this target deliberately: it is the first file moved off app.h, and the
    # only proof that it no longer needs windows.h is a non-Windows compiler
    # accepting it. Asserting that in a comment would prove nothing.
    add_executable(platform_paths_tests
        tests/platform_paths_tests.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(platform_paths_tests PRIVATE include)
    # shell32 on Windows, Qt6::Core and Qt6::Gui off it -- see PLATFORM_PATHS_LIBS.
    target_link_libraries(platform_paths_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(platform_paths_tests PRIVATE UNICODE _UNICODE)
    endif()
    if(PLATFORM_PATHS_BACKEND_QT)
        # Gui, not just Core, and for exactly one function: openInDefaultApplication
        # is QDesktopServices. Nothing here constructs a QGuiApplication, so this
        # links and runs headless -- which the test target needs, because it has no
        # display and no business launching an application.
        target_link_libraries(platform_paths_tests PRIVATE Qt6::Core Qt6::Gui)
        # The Qt backend UNIFIES one answer the two native backends disagreed about
        # (a directory's write time), so the test's per-platform assertion has to key
        # on the BACKEND rather than on the OS. Without this definition that
        # assertion would silently test the wrong thing on whichever side moved.
        target_compile_definitions(platform_paths_tests PRIVATE TINTA_PATHS_BACKEND_QT)
    endif()
    if(APPLE)
        # macOS's Qt backend does not read LANG/LC_ALL/LANGUAGE at all: QLocale::system()
        # resolves through CoreFoundation/NSUserDefaults (System Settings > Language &
        # Region), so the env-var steering the Linux branch below relies on has no effect
        # here -- measured: setenv("LANGUAGE", "zh_CN...") left the reported list at the
        # real system languages, unchanged. The one knob that DOES steer it is
        # `-AppleLanguages (tags)`, an NSUserDefaults *argument*-domain override that is
        # read from argv at first access, which is why it has to be supplied here, at
        # process launch, rather than via setenv() inside the test after main() starts.
        add_test(NAME platform_paths COMMAND platform_paths_tests -AppleLanguages "(zh-CN, ja-JP)")
    else()
        add_test(NAME platform_paths COMMAND platform_paths_tests)
    endif()

    if(NOT WIN32 AND NOT APPLE)
        # A SECOND ENTRY FOR ONE ASSERTION, because it cannot share a process with
        # the first. preferredUiLanguages drops the C/POSIX locale, and the Qt
        # backend resolves the system locale once and caches it -- so a run that
        # forces LANGUAGE to real languages can never afterwards observe the C
        # locale being filtered. Deleting that filter from the backend left the
        # single-process test green, which is what this entry fixes.
        #
        # All three variables, and the empty LANGUAGE is not redundant: measured,
        # LANG=en_AU with an empty LANGUAGE still reports en-AU, and only
        # LC_ALL=C plus LANG=C plus an empty LANGUAGE yields exactly one tag, "C".
        # This is set here rather than in the test because a test that edits its own
        # environment before the first query is the bug this replaced.
        #
        # NOT registered on APPLE: this control has no macOS equivalent. Measured every
        # override CoreFoundation exposes -- LC_ALL/LANG/LANGUAGE (no effect, see above),
        # an empty `-AppleLanguages ()` (falls back to the real system languages, not
        # empty), and `-AppleLocale C` (same fallback) -- and none of them reach a state
        # where QLocale::system() reports bare "C"/"POSIX". macOS's user-facing language
        # list is sourced from System Settings, which is never actually empty on a real
        # machine, so the state this control probes for is not reachable from a test
        # process here at all.
        add_test(NAME platform_paths_bare_locale
                 COMMAND platform_paths_tests --bare-locale)
        set_tests_properties(platform_paths_bare_locale PROPERTIES
            ENVIRONMENT "LC_ALL=C;LANG=C;LANGUAGE=")
    endif()

    # src/selection.cpp -- LINKED AND RUN off Windows, which it was not until this
    # commit. It used to be in portable_compile_only below, under a long comment
    # explaining why it could compile but never link.
    #
    # THAT COMMENT'S BLOCKER IS GONE, and it is worth recording what it was, because
    # it was the reason this file sat compile-only for ten spans: App held the
    # Direct2D backends BY VALUE -- renderTargetStorage, brushStorage,
    # bitmapStorage, textFactoryStorage and the *FormatStorage members -- so a
    # portable target that instantiated an App wanted vtables for D2DRenderTarget,
    # D2DBrush, D2DBitmap, D2DTextFactory and D2DTextFormat::~D2DTextFormat, all of
    # which live in src/render_d2d.cpp and src/text_d2d.cpp, both Windows-only by
    # design. The seam was abstract at the call sites and concrete at the storage.
    # App now owns pointers to the abstract seam types (include/seated.h), so the
    # vtable comes from whichever backend was installed and an App that is never
    # drawn needs none.
    #
    # MEASURED, in the order the errors actually appeared, because "it links now"
    # was a prediction before it was a reading and the prediction was incomplete:
    # linking tests/selection_tests.cpp against selection.cpp alone leaves FOUR
    # undefined symbols, not the three predicted -- THEMES, isWordBoundary(wchar_t),
    # and qmd::MarkdownParser's constructor and destructor -- plus tintaConfigDir()
    # once those are supplied. Every one is in a source already listed as portable,
    # so the remainder was wiring and not a second blocker. The resulting binary
    # contains ZERO D2D wrapper symbols (nm -C, grep -c, = 0), which is the property
    # the storage change bought and is checked rather than assumed.
    #
    # It DOES link md4c, and the first version of this comment said it did not --
    # reasoning that qmd::Element's constructor is inline and nothing here calls the
    # parser, so no md4c symbol could be referenced. The link disagreed: md_parse,
    # undefined. src/markdown.cpp references it whether or not this test parses
    # anything, and App holds a qmd::MarkdownParser, so its constructor and
    # destructor come along. Same as context_menu_tests and bitmap_seam_tests, which
    # both link the md4c target too -- the thing to have copied in the first place.
    add_executable(selection_tests
        tests/selection_tests.cpp
        src/selection.cpp
        src/themes.cpp
        src/text_utils.cpp
        src/markdown.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(selection_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(selection_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(selection_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME selection COMMAND selection_tests)

    # The other half of src/selection.cpp: the four functions that need a LAYOUT,
    # not just docText. Same sources as selection_tests, plus tests/app_fixture.h.
    #
    # A SECOND TARGET RATHER THAN MORE CASES IN THE FIRST, for the reason
    # code_block_layout_tests and blockquote_layout_tests are separate from their
    # neighbours: the fixture is the expensive part. selection_tests needs nothing
    # but an App and a string, and keeping it that way means a failure there is
    # unambiguously about word boundaries. A failure here is about layout
    # arithmetic, or about the fixture, and the split says which before anything is
    # read.
    #
    # WHAT MADE THIS POSSIBLE, since it is the same unlock as selection_tests and
    # the more interesting half of it: App's layout members -- textRects,
    # lineBuckets, layoutTextRuns -- are plain containers of portable structs, but
    # LayoutTextRun::layout is a shared_ptr to the ABSTRACT tinta::TextLayout. So a
    # fake layout is a class in a test header rather than a device, a factory and a
    # window. It was a shared_ptr to the abstract type before this work started;
    # what was missing was an App that could be constructed off Windows to hold it.
    #
    # NO NEW LINK DEPENDENCIES over selection_tests -- measured by building it, not
    # by reasoning about it, because the equivalent prediction for selection_tests
    # was wrong three times running. The fake layout implements the seam interface,
    # whose methods are all pure virtual, so nothing new is referenced.
    add_executable(selection_layout_tests
        tests/selection_layout_tests.cpp
        src/selection.cpp
        src/themes.cpp
        src/text_utils.cpp
        src/markdown.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(selection_layout_tests PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(selection_layout_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(selection_layout_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME selection_layout COMMAND selection_layout_tests)

    # --- In-document search, moved out of portable_compile_only ------------------
    #
    # src/search.cpp was compile-only on both platforms: nothing executed a line of
    # it. The object library below says why that is the weakest thing this repo can
    # claim about a file, and search.cpp is the first file to leave it for a running
    # target rather than for a new backend.
    #
    # THE BLOCKER WAS ONE SYMBOL, not the file's shape. search.cpp includes render.h
    # and the comment on portable_compile_only asserted it "would not LINK". A real
    # link named ten undefined symbols; nine resolve from the four portable sources
    # listed here, and the tenth is ensureLayoutComplete, still in render.cpp.
    # search_tests.cpp defines it as a stub and says so at its top, because the
    # fixture builds the layout state that function exists to guarantee. That is a
    # real limit on this target: it asserts everything downstream of the
    # precondition and nothing about the precondition itself.
    #
    # SOURCES ARE THE SAME FOUR as selection_layout_tests plus search.cpp. Arrived at
    # by attempting the link and reading the errors, not by reasoning -- an earlier
    # filtered `nm -u` under-reported the debt as eight symbols because the filter
    # excluded anything mentioning std::, which ate extractText's signature.
    add_executable(search_tests
        tests/search_tests.cpp
        src/search.cpp
        src/themes.cpp
        src/text_utils.cpp
        src/markdown.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(search_tests PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(search_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(search_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME search COMMAND search_tests)

    # --- file_utils.cpp, newly portable and newly EXECUTED ------------------------
    #
    # Two separate claims, and this target only makes one of them. That file compiles
    # off Windows as of the FILETIME seam (platform::fileWriteTicks replacing
    # CreateFileW/GetFileTime), and tests/portable-sources.txt is what asserts THAT.
    # What this target adds is execution: all five externally reachable functions had
    # zero coverage on either platform, and three of them are string arithmetic
    # carrying drive-letter assumptions that no longer hold on two of the three
    # target platforms.
    #
    # NOT app_fixture.h. file_utils.cpp reads currentFile, folderBrowserPath and
    # folderItems and touches no layout state at all, so a plain stack App is enough
    # and the fake layout would be dead weight on the link line.
    add_executable(file_utils_tests
        tests/file_utils_tests.cpp
        src/file_utils.cpp
        src/document.cpp
        src/text_utils.cpp
        src/themes.cpp
        src/markdown.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(file_utils_tests PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(file_utils_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(file_utils_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME file_utils COMMAND file_utils_tests)

    # --- TWO TESTS THAT WERE ALREADY PORTABLE AND GATED TO WINDOWS ANYWAY --------
    #
    # Both of these existed, passed, and ran only on Windows. Neither needed a line
    # of code changed to run off it: they were inside `if(WIN32)` because they linked
    # src/render_d2d.cpp and src/text_d2d.cpp, and their own comments said why --
    # "App holds wrappers BY VALUE, so any target that instantiates App needs their
    # vtables." That is a true statement about the App of ten spans ago. App now owns
    # pointers to the abstract seam types (include/seated.h), so an App that is never
    # drawn needs no backend vtable at all, and the two backend sources came off the
    # link line with nothing to replace them.
    #
    # THE PART WORTH RECORDING is not the migration, it is how it was found. When
    # selection_tests left the compile-only library, the comment there was updated to
    # say "THAT COMMENT'S BLOCKER IS GONE" -- and the identical blocker comment on
    # four other targets was left standing. Nothing failed. A stale reason for a gate
    # is invisible: the gate keeps working, the tests keep passing on the platform
    # they are allowed to run on, and the count of tests off Windows is simply lower
    # than it could be. It took a census -- g++ -fsyntax-only over every src/ file
    # not in tests/portable-sources.txt -- to notice, and the census was run for an
    # unrelated reason.
    #
    # MEASURED BEFORE BEING WRITTEN, by linking and running both off Windows with the
    # backend sources dropped: link_exit=0, run_exit=0, zero FAIL lines, and
    # inline_style_tests printing its own "All inline style tests passed". No
    # undefined symbols appeared, which is worth stating plainly because the
    # equivalent prediction for selection_tests was wrong three times running.
    #
    # NOT EVERY GATED TARGET IS A STALE GATE, and two of the four carrying that
    # comment are staying exactly where they are. tests/config_path_tests.cpp
    # includes <windows.h> deliberately, to check how configFilePathForShell copes
    # with MSIX filesystem virtualization. tests/bitmap_seam_tests.cpp asserts on
    # D2DBitmap, the Direct2D wrapper itself. Both are Windows-only by subject rather
    # than by accident, and moving them would be a worse mistake than leaving the
    # other two behind was.
    add_executable(context_menu_tests tests/context_menu_tests.cpp src/context_menu.cpp
        src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE})
    target_include_directories(context_menu_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(context_menu_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(context_menu_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME context_menu COMMAND context_menu_tests)

    # The sentinel format handles in this one are never dereferenced -- emphasisFormat
    # and inlineCodeFormat only ever get compared -- so it needed no DirectWrite even
    # on Windows, and the windowscodecs/d2d1/dwrite libraries it used to link were
    # there purely for the two backend sources.
    add_executable(inline_style_tests
        tests/inline_style_tests.cpp
        src/inline_style.cpp
        src/markdown.cpp
        src/themes.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(inline_style_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(inline_style_tests PRIVATE md4c ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(inline_style_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME inline_styles COMMAND inline_style_tests)

    # --- THE HIGHLIGHTER, ASSERTED ON BOTH PLATFORMS -----------------------------
    #
    # src/syntax.cpp had no test on EITHER platform until this target. It was in the
    # portable_compile_only library below, under a comment that said it was "the one
    # that most deserves assertions -- pure logic, no App, no seam, and not a single
    # test on any platform today". That comment was right, and this is the target it
    # was asking for; the file has left the compile-only library entirely.
    #
    # CHEAPEST TARGET IN THIS FILE, and worth saying why: syntax.cpp constructs no
    # App, calls no seam and touches no device. It needs D2DTheme (a plain aggregate
    # of colours in include/theme_data.h) and SyntaxTokenType (an enum in app.h), and
    # both are types rather than behaviour. So the link line is the test plus the one
    # source -- no md4c, no platform paths, no themes.cpp. That was NOT predicted,
    # it was measured: the equivalent prediction for selection_tests was wrong three
    # times running, going 3 -> 4 -> 5 -> 6 undefined symbols, each step found by a
    # linker rather than by reasoning. Here the linker was asked first and had
    # nothing to say.
    #
    # THE INVARIANT IS THE POINT. tokenizeLine must partition its input: the tokens
    # concatenated back together have to reproduce the line exactly. The test asserts
    # that on every line it tokenizes, which is what makes the assertion count worth
    # anything -- a hand-written lexer's characteristic bug is an off-by-one in a
    # token's span, and this catches the whole family without predicting which one.
    add_executable(syntax_tests
        tests/syntax_tests.cpp
        src/syntax.cpp
    )
    target_include_directories(syntax_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    if(WIN32)
        target_compile_definitions(syntax_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME syntax COMMAND syntax_tests)

    # --- THE TYPOGRAPHY VALUE TYPES, AND THE DIVERGENCE THEY DOCUMENT ------------
    #
    # NO SOURCES. Everything under test is a value type or an inline function in
    # include/text_api.h -- FontFeature, Typography, packOpenTypeTag,
    # qtCanHonourFeature -- so the link line is the test and nothing else. That is not
    # a shortcut; it is the shape of the decision this seam took. DirectWrite's
    # SetTypography takes an IDWriteTypography, a COM object with a lifetime, and
    # text_api.h refused it on those grounds. Measuring the population refuted the
    # objection: all of typography in this app is two fixed sets of at most three
    # OpenType tags, applied whole-range, never read back. A short list of
    # four-character tags is a value, and a value needs no device to assert.
    #
    # WHAT THIS TARGET IS ACTUALLY FOR, and it is not the packing arithmetic.
    #
    # Qt 6.4.2 has no per-font feature API -- QFont::setFeature arrived in 6.7 -- so
    # `dlig` on code spans CANNOT be honoured and is dropped, by decision rather than
    # by oversight. That is the single most invisible thing in this slice: a dropped
    # OpenType feature raises no error, returns no failure, and renders text that is
    # correct in every respect except one ligature substitution, in a font that may
    # not even ship the feature. Nothing else in this repo could ever notice. So the
    # divergence is asserted here by name, which converts a decision recorded in a
    # comment into one that fails loudly if someone later assumes all five features
    # survive the port.
    #
    # The Windows half of the file is the other reason it exists: src/text_d2d.cpp
    # says in a comment that packOpenTypeTag is the portable spelling of
    # DWRITE_MAKE_OPENTYPE_TAG and that "the two are asserted equal in
    # tests/typography_tests.cpp rather than assumed". Under _WIN32 this test includes
    # <dwrite.h> and checks exactly that, against the macro and against two of the
    # SDK's own named tag constants. Those checks are runtime rather than
    # static_assert, deliberately: a static_assert inside `#ifdef _WIN32` is
    # indistinguishable from one that never compiled, whereas a printed ok/FAIL line
    # is counted by the tally the binary prints on exit.
    add_executable(typography_tests
        tests/typography_tests.cpp
    )
    target_include_directories(typography_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(WIN32)
        target_compile_definitions(typography_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME typography COMMAND typography_tests)

    # COMPILE-ONLY, AND THAT IS THE CLAIM -- NOT THAT ANYTHING RUNS.
    #
    # SUPERSEDED 2026-09-25 BY THE KEY ROUTER, RECORDED 2026-09-29: EVERY FILE IN THIS TARGET
    # IS ALSO IN ${QINTA_APP_SOURCES}. All 21 link into qinta and qt_entry_tests, and
    # qt_entry_tests runs input.cpp through the key router and the wheel (F11 into zen
    # mode). So the per-entry notes below that say a file is "NOT in QINTA_APP_SOURCES" or
    # "cannot LINK" are the record of why it waited, not the current state; read membership
    # off the list, not off a note. LINKING IS NOT RUNNING, and a "RUNS: NO" below is
    # re-read per file: input.cpp's is superseded, overlays.cpp's still holds (nothing off
    # Windows draws an overlay; its note says which callers were checked). What this
    # target still does is compile those 21 files a SECOND time, in every configure there
    # is: a configure without Qt 6 Core, Gui and Widgets stops at FATAL_ERROR on Windows
    # and off it, so no build reaches this target without qinta. Whether to keep it is an
    # open question, not settled here. "This repo has no CI" below is superseded too:
    # .github/workflows/build.yml runs ctest on Windows, macOS and Linux.
    #
    # TWO FILES, down from four: src/syntax.cpp has left for syntax_tests above,
    # which is what this comment used to ask for. src/editrail.cpp reached zero
    # errors when its two CRT calls (swprintf_s, _countof) moved to portable_str.h
    # and std::size, and it compiles with include/ and md4c only, no Qt at all --
    # measured, not assumed.
    #
    # The other two arrived together, and how they were found matters more than
    # that they were added. A census -- g++ -fsyntax-only over every src/ file NOT in
    # tests/portable-sources.txt, with the project's own include paths -- found six
    # files at zero errors. Two of them (context_menu.cpp, inline_style.cpp) already
    # had passing tests, gated to Windows, and have moved to real targets above. One
    # (settings_app.cpp) stayed out on purpose until 2026-09-24 and is in this target
    # now; its own header says what changed.
    # These had no test to un-gate and nowhere to record the reading:
    #
    #   src/i18n_app.cpp   18 lines. One function, tr(const App&, const char*). It was
    #                     split off i18n.cpp precisely so 1500 lines of string data
    #                     could compile off Windows, and then the half that got left
    #                     behind turned out to compile too.
    #
    # NEITHER IS EXECUTED BY ANYTHING, on either platform. This target is the
    # cheapest honest way to stop the reading from rotting in the meantime.
    #
    # WHY AN OBJECT LIBRARY rather than a test: nothing executes it off Windows.
    # Without this target the green has nowhere durable to live -- this repo has no
    # CI, so the only record would be a scratch script on one machine, with
    # hand-written flags that can drift from the ones the build actually uses. This
    # target reads the file with the project's own include paths, and
    # scripts/probe-portable-linux.sh sees the /src/editrail.cpp.o compile line in
    # the build log exactly as it sees every other object -- which is what lets
    # tests/portable-sources.txt list it honestly.
    #
    # What this does NOT prove, stated because a target that compiles is easy to
    # mistake for a target that passes: no assertion in this repo executes a line of
    # editrail.cpp on EITHER platform. Its drawing goes through the render and text
    # seams, which ARE asserted against each other, so the layer beneath it is
    # tested and its own logic is not.
    #
    # selection.cpp's departure from this target is not a loss of coverage: the
    # portable probe asserts the manifest against what the build compiled, and
    # selection_tests above compiles it with the same include paths, so it is still
    # seen -- now with a linked, running binary behind it.
    #
    # src/search.cpp HAS LEFT TOO, for search_tests above, and the entry this comment
    # used to carry for it was wrong in two ways worth recording rather than deleting.
    # It said search.cpp "would not LINK" and that render.cpp was "128 native Direct2D
    # and DirectWrite mentions deep". The second figure is stale -- the same census
    # reads 93 mentions today, 75 of them outside comments, after render.cpp's text
    # path moved onto the seam. The first was true but overstated, and a written claim
    # of impossibility is the least-tested kind: a real link found exactly ten
    # undefined symbols, nine of them in translation units already on this manifest
    # (themes.cpp, text_utils.cpp, markdown.cpp, and platform_paths_posix.cpp, which
    # is DELETED as of the Qt paths span -- platform_paths_qt.cpp resolves those
    # symbols now, so the count stands and the file naming it does not). Only
    # ensureLayoutComplete is genuinely stuck in render.cpp, search.cpp calls it at one
    # site as a documented precondition, and search_tests supplies that precondition
    # from the layout fixture instead. Cost of finding that out: one link attempt.
    #
    # src/tableedit.cpp ARRIVES here on 2026-09-15, which makes this the first commit
    # in three to grow this target rather than shrink it. Worth naming as a cost: the
    # file went from 12 g++ errors to 0 off Windows because the keyboard seam took its
    # two GetKeyState calls and keymap.h gained the ten VK_ codes it uses, so it
    # genuinely compiles now -- and compiling is not testing, which is this target's
    # entire reason for existing separately.
    #
    # What stands between it and a running test was measured, not assumed: it calls
    # editorMarkDirtyAndReparse, editorPreviewVisible and editorReplaceRangeExternal,
    # all in src/editor.cpp, which is not linked into any runnable off-Windows target.
    # CORRECTED 2026-09-22: this said editor.cpp "does not compile off Windows at all
    # (fatal <imm.h>)". FALSE -- it compiles with errors 0, fatal 0, and it is a
    # tests/portable-sources.txt entry. The blocker is a LINK, and it is two files
    # further out: editor.cpp needs symbols from src/tabs.cpp and src/print.cpp, which
    # are the only two root blockers in the closure (see the note under
    # QINTA_APP_SOURCES). The old sentence sent a reader to the wrong file.
    # SUPERSEDED 2026-09-24: the 2026-09-24 fixpoint closes all 14 once print_qt.cpp, the theme hooks and
    # settings_app.cpp are in (see the note under QINTA_APP_SOURCES), so what stands
    # between this file and a running test is now only that nothing calls editor.cpp.
    # Its two pure functions, tsvLooksLikeTable and tsvToMarkdownTable, need no App and
    # no HWND and are assertable the moment that is settled.
    # SUPERSEDED AGAIN 2026-09-25: the key router is editor.cpp's caller and both it and
    # tableedit.cpp are in QINTA_APP_SOURCES, so tableedit.cpp links. Whether a test
    # executes its own logic is not claimed here.
    add_library(portable_compile_only OBJECT
        # src/annotations.cpp -- COMPILE-ONLY, and the reason is a link closure rather
        # than a compile problem. It reached 0 errors off Windows once the clipboard seam
        # landed and its wide-path ofstream became a named fs::path; those were the two
        # families its census row hid behind one label. What it still cannot do is LINK
        # into qinta: it calls handleFileWatchTimer, which lives in src/input.cpp.
        # UNTIL 2026-09-21 THIS SENTENCE ENDED "and input.cpp is windows.h-fatal", which
        # is no longer why. input.cpp compiles off Windows at 0 errors since the zen-mode
        # seam, and is itself in tests/portable-sources.txt -- but compile-only, linked
        # into no Qt target, because nothing Qt-side routes input yet. So the closure is
        # still open and this is still the honest home: the blocker moved from a fatal
        # header to an unlinked translation unit, which is render.cpp's fixpoint lesson
        # again and NOT progress this row may claim. So this is the honest home for it -- a
        # non-Windows compiler reads the file every build, which is what
        # tests/portable-sources.txt claims and all it claims.
        #
        # NOT added to QINTA_APP_SOURCES, deliberately: doing that would trade a green
        # build for a manifest entry, and the manifest is not a wish list.
        # SUPERSEDED 2026-09-25: input.cpp links into qinta with the key router, so the
        # closure this note waited on closed, and annotations.cpp went into
        # QINTA_APP_SOURCES with it.
        src/annotations.cpp
        src/editrail.cpp
        src/tableedit.cpp
        # src/settings_app.cpp -- COMPILE-ONLY. Its four functions are what src/input.cpp
        # needs from it, and nothing off Windows links input.cpp yet. It was kept out of
        # every non-Windows build as a proxy guard for settings.cpp; the probe asserts that
        # property directly now (see the file's header).
        # SUPERSEDED 2026-09-25: input.cpp links off Windows now (the key router), and
        # settings_app.cpp is in QINTA_APP_SOURCES beside it.
        src/settings_app.cpp
        # src/print.cpp -- portable since 2026-10-06, linked into qinta and run by
        # qt_entry_tests. It is here as well because this target carries no Qt include path,
        # so a Qt header creeping into the shared print code fails HERE first. The same
        # guard scroll.cpp and scrollbar.cpp are under.
        #
        # src/print_qt.cpp sat in this slot until the same day, as a compile-only file of
        # seven refusals. It left because it includes <QLocale> now, and this target
        # exists to compile without Qt.
        src/print.cpp
        # src/export_docx.cpp -- COMPILE-ONLY, and it is here for a reason no other entry
        # in this list shares: NOBODY PORTED IT. Its 1,545 lines already compiled off
        # Windows. What kept it out of this target was three includes -- <objbase.h>,
        # <ole2.h>, <wincodec.h> -- left with no caller when the 2026-09-19 clipboard slice
        # moved the CF_BITMAP read behind platform::clipboardImage, and the census stops at
        # the first unresolvable header, so the file reported `fatal 1, errors 0`: the
        # reading that is indistinguishable from finished work and means the file was never
        # read at all. Deleting the three lines took it to exit 0, fatal 0, errors 0.
        #
        # COMPILE-ONLY rather than in QINTA_APP_SOURCES, on the same rule as
        # src/annotations.cpp above: the closure, not the compile. exportAsDocx is reached
        # from src/input.cpp, which is not in QINTA_APP_SOURCES, so this file cannot LINK
        # into qinta off Windows. Adding it to the app target would trade a green build for
        # a manifest entry, and the manifest is not a wish list.
        # SUPERSEDED 2026-09-25: input.cpp is in QINTA_APP_SOURCES now (the key router), and
        # export_docx.cpp went in with it, so it links.
        #
        # CORRECTED 2026-09-22: this said input.cpp "is windows.h-fatal". FALSE -- input.cpp
        # compiles off Windows at errors 0, fatal 0 and is a manifest entry. It is out of the
        # app target because it needs six symbols only src/print.cpp defines (TRUE until
        # 2026-09-24; src/print_qt.cpp defined them from then, and src/print.cpp itself since
        # 2026-10-06; what was left is that nothing
        # calls input.cpp -- the key router). The conclusion
        # above survives; the reason did not, which is the dangerous half, because the old
        # reason pointed at a file that is already ported.
        #
        # THE CONTROL IS RECORDED BECAUSE A CLEAN FIRST READ IS A WARNING IN THIS TREE, NOT
        # A RESULT. Under the same rig, before the edit, this file read `exit 1, fatal 1,
        # errors 0` with objbase.h as its first cause -- so the subject was demonstrably
        # being reached. After it, one undeclared name planted in one function was predicted
        # at exactly 1 error (g++ reports an undeclared name once per function), read back
        # as exactly 1, then restored and verified byte-exact by sha256.
        src/export_docx.cpp
        # src/text_utils_app.cpp -- one function, measureText's seam overload, split out
        # of src/utils.cpp because a linker reading of qinta's closure found it was the
        # ONLY symbol render.cpp needed from that file.
        #
        # CORRECTED 2026-09-22: this called src/utils.cpp "windows.h-fatal". FALSE as of
        # today -- utils.cpp compiles off Windows at errors 0, fatal 0 and is a manifest
        # entry. The split was still worth doing for the reason the sentence already gives
        # (one symbol, not a whole file), so only the stale adjective is gone.
        #
        # NOT COMPILE-ONLY, AND THE PARAGRAPH THAT SAID SO WAS WRONG ABOUT ITS OWN PROBE.
        # It read "NOTHING in this tree executes measureText on any platform -- grepping
        # tests/ for the name returns nothing". The grep is still literally true (0 hits
        # for the name across tests/*.cpp) and the conclusion drawn from it was false:
        # tests/qt_entry_tests.cpp calls installQtBackends, which reaches
        # installQtTextFormats and then src/text_formats_app.cpp, whose last four lines
        # ARE this overload -- once per space width. That was already the case before the
        # document formats were de-duplicated into one translation unit, so this is a
        # correction and not a change in coverage.
        #
        # The lesson is the instrument, not the number: a name grep over tests/ measures
        # whether a test MENTIONS a function, and the property claimed was whether one
        # EXECUTES it. Those differ by every transitive call in the tree, and here they
        # differed in the direction that understates coverage -- the rarer and quieter of
        # the two. What remains genuinely unasserted is the RETURN VALUE: nothing checks
        # the widths against an expected figure, so a plausible-but-wrong measurement
        # still passes. That is the real gap and it is smaller than the one claimed.
        src/text_utils_app.cpp
        # src/text_formats_app.cpp -- the sixteen document formats, moved here out of
        # src/qt_init.cpp so the Windows initialiser could stop building its own eleven.
        #
        # NOT compile-only, unlike its neighbour above, and that is the one line worth
        # reading twice: the Qt seam tests already run this code end to end. They build a
        # real App with a live QtTextFactory, call installQtBackends, and assert the
        # formats non-null -- tests/qt_entry_tests.cpp does it around :397. So the same
        # bodies that draw a Windows page are executed off Windows by an existing test,
        # which is what a shared TU buys that two parallel copies could not.
        src/text_formats_app.cpp
        # src/render.cpp ARRIVES HERE, and it is the largest single file this port has
        # moved -- but read what that does and does not mean, because the gap between
        # the three readings is unusually wide for this one file.
        #
        #   COMPILES off Windows: measured, exit 0 / fatal 0 / errors 0, under a census
        #   whose controls all held -- de-numbered deliberately, because it said "six"
        #   and the census has since grown three more. That is what this target asserts.
        #   One qualifier the census itself now makes: exit 0 / fatal 0 / errors 0 is
        #   also what a file that compiled NOTHING reads, so the census additionally
        #   reports what the object emitted, and render.cpp reads `emits=code`.
        #
        #   LINKS into a Qt app: YES, AS OF THIS COMMIT, and the sentence here used to
        #   say "NOT YET, the residue being six symbols owned by src/math_render.cpp,
        #   which still does not compile off Windows" -- a QUOTATION of the old claim,
        #   closed here in 2026-09-22 because the unclosed quote read as a live claim and
        #   cost one sweep a misclassification. What changed
        #   is not math: src/math_stub.cpp supplies those six, and the nineteen other
        #   files the fixpoint named joined qinta with two Qt library lines.
        #
        #   AND NOW IT IS MATH TOO. That paragraph ended "the residue is still six and
        #   the file that resolves them is still a stub", which stopped being true when
        #   src/math_render.cpp was ported: it compiles off Windows, it is on
        #   QINTA_APP_SOURCES, and src/math_stub.cpp is deleted from the tree. The six
        #   symbols are resolved by the real backend, so the residue is zero rather than
        #   six. Corrected here rather than dropped, because the old sentence was the
        #   tree's own record of what the stub was standing in for.
        #
        #   RUNS: YES, AS OF THIS COMMIT, and this is the reading that was missing
        #   longest. tests/qt_entry_tests.cpp links this file, lays out a parsed
        #   document through it and counts the pixels that changed -- so "nobody has
        #   watched it", which stood here and in README for several spans, is now false.
        #
        # The distance between "compiles" and "links" is the whole reason this target
        # exists. This file used to be the sharpest example of it in the tree and is
        # now the example of it being CLOSED -- which is why the target keeps the file:
        # a Qt-free compile is still a stronger reading than the Qt-linked test, and
        # losing it would mean nothing checks that render.cpp needs no Qt.
        src/render.cpp
        # The document paint pass, here for exactly the reading above: it is linked into
        # two Qt targets, and this target is what asserts it does not NEED Qt. Cheap
        # (one extra compile) and it is the only instrument that would catch a Qt
        # include drifting into a file whose whole point is that both backends run it.
        src/paint_document.cpp
        # The scroll clamp, here for the same reason. It is one function now: the policy
        # copy of src/input.cpp's numbers that this note used to defend was deleted on
        # 2026-09-25, once the key and mouse routers made input.cpp the code both stacks
        # run (this note still said "nothing on the Qt side routes keyboard or mouse input
        # yet" two slices after that stopped being true). A Qt type drifting into
        # scroll.cpp would compile in both Qt targets, so THIS TARGET IS THE ONLY THING
        # THAT WOULD CATCH IT: qt_entry_tests links Qt and cannot tell the difference.
        src/scroll.cpp
        # The scrollbars, and this one has a Windows-flavoured edge rather than a Qt one.
        # The 95 lines came out of src/main_d2d.cpp, the least portable file in the tree,
        # and they drew through the seam already -- so the risk is not a Qt include
        # drifting in, it is a Direct2D one that was always there and that nothing had
        # ever asked about. This target is what asks.
        src/scrollbar.cpp
        # The frame, out of the same file for the same reason, and the same question.
        src/render_frame.cpp
        # THE LARGEST FILE THIS PORT HAS MOVED -- 4,153 lines, past render.cpp, and the
        # reading it earns is narrower than its size suggests, so state it in the same
        # three parts render.cpp above is stated in:
        #
        #   COMPILES off Windows: measured, exit 0 / fatal 0 / errors 0, census controls
        #   all holding, AND with a plant in this file specifically -- two undeclared
        #   DirectWrite names in two different functions, counted as exactly 2 and
        #   restored byte-exact. That plant is the only thing separating "clean" from
        #   "the census never read this file", because the census's own control plants
        #   into a scratch file rather than into the subject.
        #
        #   LINKS into a Qt app: NOT CLAIMED, and deliberately not attempted in this
        #   commit. The closure is a separate reading with its own fixpoint -- exactly
        #   the render.cpp lesson, which took three linker readings to settle.
        #
        #   CORRECTED 2026-09-22: this said "its callees include tr(), dpi() and the
        #   theme helpers, several of which still live in windows.h-fatal files". FALSE
        #   for both named symbols -- tr() is defined in src/i18n.cpp and
        #   src/i18n_app.cpp, both manifest entries that compile at errors 0/fatal 0,
        #   and dpi() is an inline in include/app.h, so it has no source dependency at
        #   all. NOT CLAIMED still stands; the reason offered for it did not.
        #
        #   RUNS: NO. Nothing executes an overlay off Windows. The overlays are keyboard-
        #   and mouse-driven chrome, and this line read "and input.cpp has not ported"
        #   until 2026-09-21, when it did -- input.cpp compiles off Windows and is the last
        #   entry in this list. The conclusion is UNCHANGED and the reason is now one step
        #   further out: what is missing is not a portable input.cpp, it is a Qt key and
        #   mouse router in src/main_qt.cpp calling into it.
        #   SUPERSEDED 2026-09-25 FOR LINKS AND FOR THE REASON, NOT FOR RUNS: overlays.cpp
        #   is in QINTA_APP_SOURCES and links, and the key router exists (src/qt_window.cpp,
        #   into input.cpp). RUNS is still NO because nothing off Windows DRAWS an overlay:
        #   fifteen of its sixteen render* functions are called only from src/main_d2d.cpp,
        #   and the sixteenth, renderAppMenuButtonBackground, only from renderTabStrip and
        #   renderEditRail, whose only callers are main_d2d.cpp and the windows-only
        #   copy_file_path test. Input that opens or scrolls one
        #   (showHelp, helpScroll) is input.cpp's code, not this file's.
        #
        # What made it affordable was that the file was already half-ported: 530 sites
        # already drew through app.brush and app.renderTarget, and the eleven remaining
        # native blocks reached for app.nativeRenderTarget/app.nativeBrush ONLY because
        # they were holding a native IDWriteTextLayout. Wiring the text factory deleted
        # the adapters rather than translating them.
        src/overlays.cpp
        # src/utils.cpp. 179 lines, and the smallest arrival here in a while -- but it is
        # the first file to reach this target by being SPLIT rather than by being ported.
        # Nothing in what remains was rewritten to compile: two functions left it, and
        # that was the whole change.
        #
        #   1. measureText(IDWriteTextFormat*) -> src/d2d_init.cpp, AND IT IS NOW DELETED
        #      OUTRIGHT, so this bullet is the record of a move whose destination no longer
        #      holds it. It moved there to sit beside the four calls in updateTextFormats
        #      that were its only callers; those four were the space-width measurements and
        #      they became the seam overload when the eleven document formats stopped being
        #      native, which left the overload with zero callers. It stayed on disk one slice
        #      longer than the callers because a removal's falsifier is a link error in
        #      another target rather than a page hash -- that link was then run, clean across
        #      51 link actions, and the function is gone. Keep the bullet: it is why this
        #      file needed dwrite.h, and its portable twin already lived in
        #      src/text_utils_app.cpp, so the file was unbuildable off Windows for one
        #      function that had already been replaced for everyone else.
        #   2. launchQuickNoteWindow -> src/quicknote_win32.cpp, which is Windows-only and
        #      says why in its own header: the flags it passes have no parser in
        #      src/main_qt.cpp, so a Qt half would silently open the wrong window.
        #
        # Two Win32 calls were ROUTED rather than moved -- SetWindowTextW to
        # win32_api.h::setWindowTitle, ShellExecuteA to platform::openExternalUrl -- and
        # GetFullPathNameW became std::filesystem::absolute().lexically_normal(), which
        # is the tree's existing spelling (src/inline_style.cpp:69) and whose suffix is
        # load-bearing off Windows only. The comment on absoluteFilePath explains why the
        # Windows test that covers it cannot fail on the suffix.
        #
        # COMPILE-ONLY, AND THE BLOCKER WAS PREDICTED BEFORE IT WAS READ, which is the
        # only reason the reading is worth anything. The prediction: it compiles clean and
        # will not link portably because copyFilePath calls signalPushKey out of
        # src/signals.cpp, which is not on tests/portable-sources.txt. Measured with
        # `nm -u` on the object: SIX undefined symbols outside the CRT, and five of them
        # -- toWide, tr, ensureLayoutComplete, platform::setClipboardText,
        # platform::openExternalUrl -- resolve from text_utils.cpp, i18n_app.cpp,
        # render.cpp, clipboard_qt.cpp and platform_paths_qt.cpp, every one of which is on
        # the manifest and linked off Windows. signalPushKey is the only one that is not,
        # and ensureLayoutComplete is specifically NOT a second blocker despite living in
        # the largest file in the tree -- render.cpp ported already.
        #
        # THIS COMMENT USED TO END "so src/signals.cpp is the single file between this line
        # and a linked one". src/signals.cpp is now the very next entry, and utils.cpp did
        # not move: signals.cpp arrived with three blockers of its own. The prediction was
        # right about the symbol and wrong about what closing it would buy, so the wording
        # is corrected rather than deleted -- "the single file between" assumes the frontier
        # is a queue, and it is a graph in which porting a leaf can add a level.
        #
        # copyFilePath IS NOT MOVED OUT to close that gap. It would make this file fully
        # linkable and would delete the copy-path feature from the Qt build to make a
        # manifest line read better. The gap is a measurement, not a target.
        src/utils.cpp
        # src/signals.cpp -- the port whose SIZE was the finding. Its census row read
        # `fatal shellapi.h` over 837 lines, and both numbers meant NOT MEASURED: g++ quit
        # at line 7. Stubbing that one header and lifting the build's own flags read the
        # whole file for the first time -- 7 errors, 3 causes, about five lines. The two
        # swprintf_s calls went to tinta::swprintf, ShellExecuteW was ROUTED to
        # platform::openExternalUrl, and GetLocalTime became localtime_s/localtime_r behind
        # an #ifdef rather than a fourth seam, because a seam is two files and a declaration
        # and this is one file-local call with one caller.
        #
        # COMPILE-ONLY, predicted first and confirmed: three undefined symbols from two
        # files that are not in QINTA_APP_SOURCES -- draftsRecoverAll and draftsDiscardAll
        # (src/drafts.cpp) and saveEditorFile (src/editor.cpp). That is the next question
        # this list poses, and it is a question, not a plan.
        #
        # CORRECTED 2026-09-22, two claims in one sentence: it said those two files were
        # "not on the manifest" and called drafts.cpp "<shlobj.h>-fatal". BOTH FALSE --
        # drafts.cpp and editor.cpp are manifest entries and each compiles off Windows at
        # errors 0, fatal 0. They are out of the app target for a link reason, not a compile
        # one: both are blocked through src/tabs.cpp and src/print.cpp. (SUPERSEDED
        # 2026-09-24: both link now in the fixpoint; nothing calls them, which is the rule
        # that keeps them out of the app target. SUPERSEDED AGAIN 2026-09-25: the key router
        # is their caller and both are in QINTA_APP_SOURCES.)
        #
        # CORRECTED 2026-09-17: this comment said src/editor.cpp "carries no Windows include
        # at all". FALSE, and measured false the next pass -- it is `#include <imm.h>` at
        # editor.cpp:28, IME support. The claim came from a grep whose pattern was a
        # hand-written list of header names, and imm.h was not on the list, so no match got
        # read as no include. An allowlist cannot report what it does not list; the honest
        # instrument is the compiler, which says `fatal error: imm.h` in one line. The point
        # the sentence was making survives -- nothing has read editor.cpp end to end -- but
        # it now rests on the fatal include rather than on its absence.
        src/signals.cpp
        # src/drafts.cpp -- the second of signals.cpp's three blockers, and the file that
        # showed what "5 of 6 causes already have destinations" looks like. Sized by
        # COMPILING it, never by its census row: `fatal shlobj.h` over 160 lines became 29
        # errors from SIX causes once that one header was stubbed.
        #
        # Five went to spellings the tree already had. CreateDirectoryW, DeleteFileW x4 and
        # the whole FindFirstFileW/WIN32_FIND_DATAW/FindNextFileW/FindClose block went to
        # <filesystem>, which 23 files under src/ already include; the two wide-path fstream
        # constructors -- an MSVC extension with no POSIX spelling -- went through
        # std::filesystem::path, the route file_utils.cpp:190 documents and :238 uses; the
        # two swprintf_s calls went to tinta::swprintf's array overload; four hardcoded
        # L'\\' path joins went to platform::pathSeparator(); and _wtoi became std::wcstoul.
        #
        # THE SIXTH HAD NO DESTINATION AND IS AN #ifdef IN PLACE: draftOwnerIsLiveSibling asks
        # "is this pid a running copy of this executable", so a live sibling's autosave is not offered as a
        # crash leftover. OpenProcess/QueryFullProcessImageNameW appear at exactly one site
        # in the tree, so a seam would be two files and a declaration for a decision that
        # does not recur. The arms are NOT equivalent and the file says so at the site: Linux
        # reads /proc/<pid>/exe; anywhere without /proc every pid reads as not-ours, which
        # fails in the direction Windows already fails when OpenProcess is refused -- a draft
        # gets offered, never lost.
        #
        # <shlobj.h> was DELETED rather than guarded. Nothing here ever called a shell-folder
        # function; the header's only job was dragging in windows.h, which this file already
        # reaches on Windows through drafts.h -> app.h -> <d2d1.h>.
        src/drafts.cpp
        # src/editor.cpp -- 3,135 lines, the file the three lines above were all waiting on:
        # saveEditorFile blocked signals.cpp and enterRecoveredDraft blocked drafts.cpp.
        #
        # ITS SURFACE HAD NEVER BEEN READ UNTIL THIS PASS, and the comment above says why:
        # `#include <imm.h>` was fatal, so every figure this project published about this
        # file was a proxy. Stubbed and compiled with the build's own flags it read 55
        # errors on 40 lines. **That was not the whole story either.** Routed, it read 7
        # more, then 0 -- 55 -> 7 -> 0 -- so the honest sizing of this file took two passes
        # and the first pass's seven-family list was wrong in four places. An error count is
        # not a cause count, and a cause list from one pass is not a census.
        #
        # Five families went to destinations the tree already had: 7 GetKeyState sites to
        # include/keyboard.h's isShiftDown/isCtrlDown, 4 wide-path fstream constructors
        # through std::filesystem::path, 2 swprintf_s + 2 _countof to tinta::swprintf with
        # std::size, _wtoi to std::wcstol, SetWindowTextW to win32_api.h::setWindowTitle,
        # CopyFileW to std::filesystem::copy_file (its FALSE was bFailIfExists, so
        # overwrite_existing is the carry), and RenderTarget::DrawTextW to the seam's real
        # name DrawText -- a rename only, since <winuser.h>'s UNICODE macro rewrites the
        # token straight back on Windows, exactly as render_api.h documents.
        #
        # THREE THINGS NEEDED NEW SURFACE, all of it narrow. The cursor and mouse-capture
        # families (26 + 6 sites) became setCursorShape/captureMouse/releaseMouse inlines in
        # win32_api.h -- justified by RECURRENCE and measured before being built: 133 such
        # sites tree-wide, and input.cpp holds 87 of them to editor.cpp's 39, so they serve
        # the NEXT slice more than this one. WM_CLOSE became a constant in win32_types.h
        # beside WM_APP, with the same Windows-side static_assert. And the clipboard seam
        # gained clipboardHasImage -- the availability question only, never the image data.
        #
        # ONLY THREE VK_ CODES WERE ACTUALLY MISSING, against a first-pass estimate of 26
        # edits: keymap.h already defined 18, including THREE of the four arrow keys. The gap
        # at 0x26 was VK_UP, absent because no file measured before today named it.
        #
        # <imm.h> was GUARDED, not deleted -- the opposite call to drafts.cpp's <shlobj.h>,
        # and for the opposite reason: it has a real caller. editorPositionImeWindow is the
        # entire IME surface, six identifiers with one occurrence each in one 35-line
        # function, so the guard went round the function. Off Windows CJK composition still
        # works; what is lost is the candidate window's POSITION.
        #
        # COMPILE-ONLY, and the one symbol that makes that precise is launchQuickNoteWindow:
        # utils.h declares it under _WIN32 deliberately, so that an off-Windows caller is a
        # compile error at the site. This file is that caller and the site now answers it --
        # Ctrl+Shift+N does nothing off Windows, and deliberately does NOT fall through to
        # the new-tab branch beside it, which would be a wrong picture rather than a gap.
        src/editor.cpp
        # src/startpage.cpp -- 792 lines, and THE FILE THAT REFUTED THIS PROJECT'S OWN
        # PREDICTION ABOUT IT. The plan said the port would take MORE THAN ONE PASS,
        # citing editor.cpp's 55 -> 7 -> 0 directly above and this file's own first
        # reading: 118 errors on 102 lines, 64 undeclared names, 29 of them cascade, 35
        # genuine causes. It took ONE. Recorded here rather than quietly enjoyed,
        # because the reason is a property worth knowing and not luck.
        #
        # THE REASON IS THAT THE DIRECTWRITE SURFACE WAS ALREADY FUNNELLED. Every one of
        # the ~19 text sites in renderStartPage goes through three file-local helpers --
        # spLayout, spDraw, spMetrics -- so converting those three left the call sites a
        # pure type-name substitution with nothing behind it to cascade. editor.cpp's
        # second wave came from 35 causes spread across its body, where a routed call
        # revealed the next non-portable thing beneath it; here the 35 causes were six
        # families all reaching the platform through the same three doors. A count of
        # causes does not predict the number of PASSES -- their CONCENTRATION does, and
        # that is a distinct measurement nothing had taken.
        #
        # ONE CONSTRUCT WAS GENUINELY MISSING FROM THE SEAM, and it is the one a previous
        # slice documented its refusal to add: SetCharacterSpacing, used for the
        # wordmark's -1 DIP tracking and the RECENT/LEARN labels' +1.5. text_api.h had
        # declined it because it "needs an IDWriteTextLayout1 obtained by QueryInterface"
        # -- true only while this file was a Windows file. The QI did not go away; it
        # moved to text_d2d.cpp where COM belongs. It is narrowed to ONE float against
        # DirectWrite's three, because Qt has one letter-spacing knob and every call site
        # in the tree passed literal zeros for the other two.
        #
        # TWO ARMS STAY WINDOWS-ONLY IN PLACE, both introducing no regression because
        # this file has never compiled off Windows at all. startPageEnsureIcon loads an
        # EMBEDDED WIN32 RESOURCE and image_api.h's only decode entry point takes a file
        # path, so there is nothing portable to call yet; off Windows the hero draws
        # without its mark, and the shared destination is its own slice (three consumers
        # at two pixel sizes, plus a .qrc this build does not have). And the "MMM d" date
        # keeps GetDateFormatEx verbatim, because it renders in the USER'S locale and
        # strftime cannot -- the #else says so at the site rather than hiding it.
        #
        # THE ONE NEW HEADER, include/portable_time.h, EXISTS BECAUSE signals.cpp
        # PREDICTED IT. That file wrote its own localtime_s/localtime_r split and said a
        # portable_time.h would then be "a header with one caller"; relativeWhen is the
        # second caller, so the recurrence test it named is met and signals.cpp now
        # routes through it too.
        #
        # COMPILE-ONLY, and the symbol that makes that precise is startPageOpenEmbedded's
        # HWND parameter -- portable already, since win32_types.h forward-declares the
        # handle type. What is not linkable is the icon arm's Win32/WIC calls.
        src/startpage.cpp
        # src/pandoc.cpp -- COMPILE-ONLY, for the same class of reason as
        # src/annotations.cpp at the top of this list and NOT because anything in it is
        # still Windows-shaped. The process seam (include/platform_process.h) took its
        # last Win32 call on 2026-09-20, so a non-Windows compiler now reads the whole
        # file including pandocWorker's body, which it never could before: that function
        # was declared `DWORD WINAPI pandocWorker(LPVOID)` and failed at its signature,
        # so g++ reported nothing whatever about the eleven calls inside it.
        #
        # WHAT KEEPS IT OUT OF ${QINTA_APP_SOURCES} is a link closure and one honest gap.
        # pandocExportFlow calls editorReparse in src/editor.cpp, which is not in that
        # target, so the file cannot link into qinta at all yet. SUPERSEDED 2026-09-25:
        # editor.cpp and pandoc.cpp are both in QINTA_APP_SOURCES (the key router), so it
        # links; the honest gap in the next paragraph is what remains.
        #
        # CORRECTED 2026-09-22: this said editor.cpp "is windows.h/imm.h-fatal". FALSE --
        # editor.cpp is a manifest entry and compiles off Windows at errors 0, fatal 0.
        # What holds it out is src/tabs.cpp and src/print.cpp, the only two root blockers
        # in the closure; see the note under QINTA_APP_SOURCES. This sentence is the one
        # that would have sent the next reader at editor.cpp instead. (SUPERSEDED
        # 2026-09-24: that closure now links, so the reason left is no caller.) And even
        # once it could, the worker's last act is postWindowMessage(hwnd,
        # WM_APP_PANDOC_DONE, ...), whose non-Windows arm returns false by
        # include/win32_api.h's stated design -- the conversion would run and finish and
        # the UI would never be told. Delivering that needs the Qt window-message seam,
        # which is its own slice and is where the HWND in PandocJob goes.
        #
        # So the claim this line makes is exactly the one tests/portable-sources.txt
        # makes and no more: a non-Windows compiler accepts the file every build.
        src/pandoc.cpp
        # src/input.cpp -- 4,586 lines, the largest file this port has moved, and THIS LINE
        # IS HERE BECAUSE THE PROBE REFUSED THE MANIFEST ENTRY WITHOUT IT. The zen-mode
        # slice added input.cpp to tests/portable-sources.txt and stopped there; nothing
        # off Windows compiled it, so scripts/probe-portable-linux.sh read
        # `MISSING: input.cpp` on its every-listed-file-was-compiled check. A manifest line
        # without a compiler behind it is a claim with no instrument -- exactly what that
        # both-direction check exists to catch, and it caught it in the same slice that
        # wrote the entry.
        #
        #   COMPILES off Windows: measured, 0 errors, and the SIZE IS THE FINDING. This
        #   file was carried as a 4,553-line port for several passes; the real cost was
        #   FIFTEEN LINES. g++ reported 29 errors before the pass, decomposed 24 in
        #   toggleZenMode / 3 in handleKeyDown / 1 in settingsAction / 1 in handleMouseUp,
        #   and 13 of the 15 lines sat inside one 21-line function. A source census over the
        #   same file found 32 sites to the compiler's 15, and SIXTEEN OF THE SEVENTEEN it
        #   saw that g++ did not were COMMENTS -- the a-comment-is-a-census-hit family, 11th
        #   firing. Anything sized by grepping this file for Win32 names overstated it by
        #   two orders of magnitude.
        #
        #   LINKS into a Qt app: NO -- and MEASURED 2026-09-21, which is what corrects the
        #   rest of this entry. The conclusion above was right and the reason it used to give
        #   was two-thirds stale, which is the a-right-conclusion-hides-a-wrong-reason family:
        #   it named editor.cpp, drafts.cpp and tabs.cpp, and editor.cpp and drafts.cpp have
        #   BOTH been ported since (`grep -cx` against tests/portable-sources.txt returns 1
        #   for each). Only tabs.cpp of the three still stands. THE COMMENT IS A CLAIM AND SO
        #   IS ITS REASON -- THEY FAIL SEPARATELY, and either way of trusting this sentence
        #   cost time: hunting editor.cpp finds nothing, and dismissing the warning as stale
        #   hits the wall it failed to name.
        #
        #   The measurement, by `nm` over the object rather than by reading this file: 276
        #   undefined symbols in input.cpp.o, 236 of them unsatisfied by the qinta target's
        #   own 48 objects. The wall the old reason MISSED is src/print.cpp -- input.cpp calls
        #   openPrintPreview and closePrintPreview, nothing else in the tree defines them, and
        #   print.cpp stops the compiler dead at src/xps_page_emitter.h:24 with
        #   `fatal error: d2d1.h: No such file or directory`. That is the one that matters:
        #   tabs.cpp is a windowing port, but print.cpp needs the QPrinter arm, so the key
        #   router IS gated on a Direct2D removal after all.
        #
        #   NO LONGER, AS OF 2026-09-24, AND THE WAY OUT WAS NOT THE ONE THIS NAMED. The
        #   router is gated on the SYMBOLS, not on a QPrinter port: src/print_qt.cpp defines
        #   the seven, each refusing, and the link fixpoint closes (26 of 26 candidates, and
        #   the same run without print_qt drops this file in round 1). Printing off Windows is
        #   still absent -- a stated gap in that file -- but it no longer holds the router.
        #   (2026-10-06: print.cpp compiles off Windows and defines the seven itself. Preview,
        #   PDF export and the link peek work on qinta. printDocument followed the same day, through QPrinter.)
        #
        #   What this does NOT claim is a complete blocker list. Attributing all 236 symbols
        #   to defining files over-attributed badly (substring matching invented 32 `*_tests.cpp`
        #   rows), so this names the two walls it can stand behind and not a count.
        #
        #   RUNS: NO, and this is the part no line count reaches. src/main_qt.cpp routes no
        #   keyboard or mouse events, so nothing calls a handler in here. F11 reaching
        #   src/zen_mode_qt.cpp needs a Qt key router. THAT SUCCESSOR SLICE RAN ON 2026-09-21
        #   AND CAME BACK BLOCKED: it delivered the repaint seam (include/repaint_sink.h, so
        #   requestRepaint's off-Windows arm is no longer `(void)hwnd;`) and could NOT deliver
        #   the router, for the print.cpp reason measured above. A successor named in a comment
        #   is a prediction, and this one was half right.
        #   The ONE thing that would be silent if wrong: win32_api.h::requestQuit's
        #   off-Windows arm is a placeholder returning false, unreachable only because of
        #   this missing router.
        #   SUPERSEDED 2026-09-25, BOTH HALVES: RUNS is YES. src/qt_window.cpp's keyPressEvent
        #   hands every key to handleKeyDown and handleCharInput, and its wheelEvent calls
        #   handleMouseWheel; qt_entry_tests sends F11 through it into toggleZenMode. And
        #   requestQuit's off-Windows arm is tinta::deliverQuitRequest() through the window
        #   sink, not a placeholder -- qt_entry_tests asserts both the no-sink false and the
        #   delivered quit.
        src/input.cpp
        # src/tabs.cpp -- COMPILE-ONLY since 2026-09-23. The tear-off pass took it to errors 0
        # off Windows, and a manifest entry needs a compiler behind it (input.cpp's MISSING
        # lesson above). It is NOT in QINTA_APP_SOURCES: qinta's own link line reads 11
        # undefined symbols with tabs.o added, and nothing in the Qt app calls it, so the rule
        # at the root-blocker note applies. Tear-off, pin and the maximise glyph are site-guarded
        # gaps off Windows.
        # SUPERSEDED 2026-09-25: tabs.cpp is in QINTA_APP_SOURCES with the key router, and it
        # links. The site-guarded gaps above still hold.
        src/tabs.cpp
    )
    target_include_directories(portable_compile_only PRIVATE include ${md4c_SOURCE_DIR}/src)
    # Unconditional, same argument as qinta above: a version number is not a platform fact.
    target_compile_definitions(portable_compile_only PRIVATE ${TINTA_VERSION_DEFINITIONS})
    if(WIN32)
        target_compile_definitions(portable_compile_only PRIVATE UNICODE _UNICODE)
    endif()

    # The Windows-only test block that stood here built input_tests, file_path_tests and the
    # Direct2D backend tests (bitmap_seam, text_factory) from ${SOURCES}. The first two moved to
    # the Qt backends on 2026-10-06; the rest left with the Direct2D stack on 2026-10-07.

    add_executable(math_parser_tests tests/math_parser_tests.cpp src/math_parser.cpp src/math_macros.cpp)
    target_include_directories(math_parser_tests PRIVATE src)
    add_test(NAME math_parser COMMAND math_parser_tests)

    # math_layout_tests moved to the Qt backends on 2026-10-06 (tinta_qt_app_test, Qt block below).

    add_executable(mermaid_tests
        tests/mermaid_tests.cpp
        src/mermaid.cpp
        src/mermaid_ext.cpp
        src/mermaid_seq.cpp
        src/mermaid_pie.cpp
        src/mermaid_uml.cpp
        src/mermaid_git.cpp
        src/mermaid_gantt.cpp
        src/mermaid_charts.cpp
        src/mermaid_narrative.cpp
        src/mermaid_grid.cpp
        src/mermaid_charts2.cpp
        src/mermaid_block.cpp
        src/mermaid_c4.cpp
    )
    target_include_directories(mermaid_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME mermaid_parser COMMAND mermaid_tests)

    add_executable(document_tests
        tests/document_tests.cpp
        src/document.cpp
        src/markdown.cpp
    )
    target_include_directories(document_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(document_tests PRIVATE md4c)
    add_test(NAME document_types COMMAND document_tests)

    # i18n_tests moved out of the if(WIN32) block below when i18n.cpp came off
    # app.h. tests/i18n_tests.cpp itself never needed Windows -- it includes
    # i18n.h, <iostream> and <string> and nothing else -- so the guard was there
    # purely because the file it links against pulled in windows.h. A non-Windows
    # compiler accepting src/i18n.cpp is the only real proof of that, which is why
    # the target moved rather than a comment being added.
    # src/i18n_app.cpp is here, and not in portable_compile_only, because a
    # forwarding function is exactly the kind of code that compiles forever while
    # being wrong. tr(const App&, key) reads app.currentLanguageIndex and hands it
    # to the index overload; drop that read and every language silently becomes
    # English, with no error, no crash and no failing build anywhere in the tree.
    # Nothing executed a line of this file on either platform until now.
    #
    # The two extra sources below are NOT i18n's dependencies -- they are App's.
    # Constructing an App needs exactly three symbols from outside this target:
    # THEMES (src/themes.cpp) and qmd::MarkdownParser's constructor and destructor
    # (src/markdown.cpp). Measured by relinking with each candidate removed, not by
    # reasoning about includes.
    #
    # That measurement is here because the first version of this list had FOUR
    # sources: it was copied from selection_layout_tests, which is the nearest
    # precedent for constructing an App off Windows. It linked first time with zero
    # undefined symbols -- and src/text_utils.cpp was not needed at all. A
    # successful link proves a list is SUFFICIENT and says nothing about whether it
    # is MINIMAL, so the only way to know is to remove each entry and watch what the
    # linker says. Dropping themes.cpp yields 1 undefined symbol, markdown.cpp 2,
    # both 3, and text_utils.cpp 0. A source list copied from a precedent is a
    # guess, in the same way the search.cpp estimate was.
    add_executable(i18n_tests
        tests/i18n_tests.cpp
        src/i18n.cpp
        src/i18n_app.cpp
        src/themes.cpp
        src/markdown.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(i18n_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(i18n_tests PRIVATE md4c)
    # shell32 for ShellExecuteW/SHGetFolderPathW inside the win32 seam, Qt off Windows.
    target_link_libraries(i18n_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(i18n_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME i18n COMMAND i18n_tests)

    # src/export_html.cpp -- RUN, and this is the first exporter to leave
    # portable_compile_only for a target with assertions in it.
    #
    # WHY IT IS WORTH A TARGET AND NOT JUST A MANIFEST LINE. Every other exporter
    # needs a device: docx rasterizes through the bitmap seam, pdf rides the print
    # pipeline. HTML is pure string work -- element tree in, one UTF-8 document out
    # -- so the whole of exportHtmlFile executes here, escaping and theme CSS and
    # the file write included. A compile-only entry would have said only that GCC
    # accepts the syntax, which is the weakest claim this repo makes about a file.
    #
    # SIX SYMBOLS ARE STUBBED IN THE TEST, not linked: exportDocxFile,
    # exportPdfFile, mathParse, mathBoxSvg, resolveDiagramRole and signalPushKey.
    # All six live in Windows-only sources today. That is a real limit on this
    # target and it is stated in the test's own header comment -- but the stubs
    # RECORD their calls and the test asserts none of them fired, so "a plain
    # markdown export never enters those paths" is measured here rather than
    # assumed. If a future change routes plain export through math or the docx
    # dispatch, this target fails and names the symbol.
    #
    # THE SOURCE LIST IS MINIMAL, MEASURED BY REMOVAL -- and the first version of it
    # was NOT, which is the more useful half of this note. It had FIVE extra entries
    # (file_utils.cpp, i18n.cpp, i18n_app.cpp, inline_style.cpp, syntax.cpp), it
    # linked first time with zero undefined symbols, and all 44 assertions passed.
    # That is the failure recorded at i18n_tests above repeating itself: a successful
    # link proves a list is SUFFICIENT and says nothing about whether it is MINIMAL.
    # The five were picked by reading export_html.cpp's #includes, which is a guess
    # about what a file might reference rather than a reading of what it does.
    #
    # ONE-AT-A-TIME REMOVAL FINDS A LOCAL MINIMUM, NOT A GLOBAL ONE, and this target
    # is the proof: the first pass reported 4 removable and i18n.cpp NEEDED, at one
    # undefined symbol. That symbol was referenced by i18n_app.cpp, one of the four --
    # so dropping the four made a fifth entry removable, which the first pass could
    # not have seen. Minimality has to be re-measured after every removal, not read
    # off one sweep. Second pass: 0 removable, on a clean build directory because
    # stale objects from the previous list are still on disk otherwise.
    #
    # Final reading, 21 objects, 0 removable, with omitting export_html.cpp itself as
    # the positive control (3 undefined). Undefined-symbol counts without each entry,
    # lowest first: themes.cpp, config_dir.cpp, document.cpp, file_dialog_qt.cpp and
    # five mermaid families 1 each, mermaid_charts/charts2 2, export_html.cpp,
    # mermaid_c4/grid/narrative/uml 3, mermaid.cpp 5, markdown.cpp 9,
    # text_utils.cpp 12, platform_paths_qt.cpp 16, mermaid_ext.cpp 324.
    #
    # syntax.cpp being removable is a statement about the FEATURE, not just the link:
    # HTML export references no symbol in the highlighter, so exported code blocks
    # come out unhighlighted. That is the current behaviour and this target now pins
    # it -- add a highlighted-code assertion here the day that changes.
    #
    # Every mermaid parser IS needed, because export_html.cpp defines
    # buildDiagramPrims and buildFlowchartPrims, so it references the whole family
    # whether or not the test document contains a diagram.
    add_executable(export_html_tests
        tests/export_html_tests.cpp
        src/export_html.cpp
        src/document.cpp
        src/markdown.cpp
        src/themes.cpp
        src/text_utils.cpp
        src/config_dir.cpp
        src/mermaid.cpp
        src/mermaid_ext.cpp
        src/mermaid_seq.cpp
        src/mermaid_pie.cpp
        src/mermaid_uml.cpp
        src/mermaid_git.cpp
        src/mermaid_gantt.cpp
        src/mermaid_charts.cpp
        src/mermaid_narrative.cpp
        src/mermaid_grid.cpp
        src/mermaid_charts2.cpp
        src/mermaid_block.cpp
        src/mermaid_c4.cpp
        ${PLATFORM_PATHS_SOURCE}
        # The picker seam, and a REAL backend rather than a seventh stub. It is here
        # because exportDocumentAs -- which this test never calls -- references
        # platform::requestSavePath, and a linker does not care which function in a
        # translation unit needed the symbol. Worth noting as the seam paying for
        # itself: before last commit this line would have had to be
        # src/export_html.cpp's OPENFILENAMEW block, which cannot link here at all.
        ${PLATFORM_FILE_DIALOG_SOURCE}
    )
    target_include_directories(export_html_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(export_html_tests PRIVATE md4c
        ${PLATFORM_PATHS_LIBS} ${PLATFORM_FILE_DIALOG_LIBS})
    if(WIN32)
        target_compile_definitions(export_html_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME export_html COMMAND export_html_tests)
    if(NOT WIN32)
        # Same reasoning as file_dialog: this binary links Qt Widgets through the
        # picker backend, and a Qt binary with no QT_QPA_PLATFORM aborts in WSL
        # trying to reach a display. Nothing here constructs a QApplication or a
        # dialog, so it would very likely survive without the variable -- set
        # anyway, because "very likely" is not a reading.
        set_tests_properties(export_html PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # settings.cpp -- the settings.ini reader and writer -- built and run off
    # Windows. It reached windows.h only through settings.h -> app.h; the App
    # coupling now lives in settings_app.cpp and the file-association feature in
    # settings_win32.cpp, and neither is linked here. Note what that means: if
    # anything drags either back into this file, THIS target is what fails, and
    # only when built off Windows.
    #
    # Run through run_portable_test.cmake rather than directly, because the
    # assertions call saveSettings. That script copies the binary to a scratch
    # directory with a settings.ini beside it, which switches tintaConfigDir()
    # into portable mode -- so the test writes its own config and never the user's.
    # The test refuses to run if that isolation is not in place.
    add_executable(settings_tests
        tests/settings_tests.cpp
        src/settings.cpp
        src/keymap.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(settings_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(settings_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(settings_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME settings_roundtrip COMMAND ${CMAKE_COMMAND}
        -DTEST_BINARY=$<TARGET_FILE:settings_tests>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/settings-test-$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_test.cmake)

    # themes.cpp -- the built-in palette table and the themes.ini reader/writer --
    # built and run off Windows. It reached windows.h only through app.h, and it
    # touches no App member whatsoever; the block it wanted is now theme_data.h.
    # Note what this target is for: MSVC compiles themes.cpp either way, so a
    # green Windows build says nothing about whether the wide-path streams and the
    # _wcsicmp replacement inside it are portable. Only a build here can.
    #
    # Runs through run_portable_test.cmake because the assertions call
    # saveCustomTheme, which rewrites themes.ini in the config home. The scratch
    # copy plus a settings.ini beside it switches tintaConfigDir() into portable
    # mode, so the test writes its own file and never the user's. It refuses to
    # run if that isolation is not in place.
    add_executable(theme_data_tests
        tests/theme_data_tests.cpp
        src/themes.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(theme_data_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(theme_data_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(theme_data_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME theme_data COMMAND ${CMAKE_COMMAND}
        -DTEST_BINARY=$<TARGET_FILE:theme_data_tests>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/theme-data-test-$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_test.cmake)

    # portable_str.h -- the case-insensitive compare and the bounded wide
    # formatting, on both backends. This header had NO test at all until now, which
    # is why the target exists: tinta::strnicmp had been shipped and adopted with
    # its contract unasserted on either platform.
    #
    # Header-only, so the target compiles nothing but the test itself, and it needs
    # no run_portable_test.cmake isolation because the assertions touch no file and
    # no config home. The reason it must run on BOTH platforms is the seam's only
    # real claim -- that the Windows and POSIX spellings behave alike -- which a
    # single-platform green cannot support.
    #
    # Truncation is deliberately absent from the assertions: it fail-fasts on
    # Windows (0xC0000409) and abort()s off it by design, so it would end the run
    # rather than test it. It is probed with a standalone executable per platform.
    add_executable(portable_str_tests
        tests/portable_str_tests.cpp
    )
    target_include_directories(portable_str_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(WIN32)
        target_compile_definitions(portable_str_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME portable_str COMMAND portable_str_tests)

    # src/text_utils.cpp -- the text helpers lifted out of src/utils.cpp, built
    # and RUN off Windows. Until this target they were portable logic (the GitHub
    # slug rules, the word-boundary predicate, the element-tree flattener) that
    # no non-Windows compiler had ever seen, because utils.cpp includes
    # <windows.h> and they happened to live in it.
    #
    # It links the platform path seam because toWide and slugifyHeading convert
    # through platform::fromUtf8/toUtf8. It does NOT link src/markdown.cpp:
    # qmd::Element's constructor is inline in the header, so the assertions build
    # their element trees with nothing but <memory>.
    #
    # No run_portable_test.cmake isolation: nothing here touches a file or a
    # config home.
    add_executable(text_utils_tests
        tests/text_utils_tests.cpp
        src/text_utils.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(text_utils_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(text_utils_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    if(WIN32)
        target_compile_definitions(text_utils_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME text_utils COMMAND text_utils_tests)

    # include/seated.h -- App's seam-wrapper storage, asserted with fake backends
    # on both platforms.
    #
    # This target exists because the slice that introduced Seated<> could only be
    # verified by the Windows app linking and drawing. The seating logic itself had
    # no assertion: its only callers are the seat* functions in app.h, whose only
    # caller is src/d2d_init.cpp, which is Windows-only and absent from
    # tests/portable-sources.txt. The property everything rests on -- that
    # reseating an UNCHANGED native does not rebuild the wrapper, because the
    # wrapper OWNS the native and would Release it -- is invisible to both the app
    # running correctly and the compiler.
    #
    # It compiles ONE header and no sources: seated.h includes only <memory> and
    # <utility> and names no backend, so this target is also the standing check
    # that it stays that way. If someone adds a D2D or Qt include to it, this stops
    # configuring off Windows rather than being discovered later by the app link.
    add_executable(seated_tests
        tests/seated_tests.cpp
    )
    target_include_directories(seated_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(WIN32)
        target_compile_definitions(seated_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME seated COMMAND seated_tests)

    # include/file_dialog.h -- the Open/Save picker seam's filter encoding.
    #
    # EVERY ASSERTION HERE IS ON THE HEADER, AND THE BACKEND IS ON THE LINK LINE FOR
    # A DIFFERENT REASON ENTIRELY. Worth separating, because the two seams either side
    # of this one look alike and are not. keyboard_tests links its backend because
    # isShiftDown has a reachable claim on both halves (it returns false with nothing
    # held). requestOpenPath and requestSavePath have NO reachable claim at all:
    # neither returns until a human clicks a dialog. So nothing below asserts the
    # backend, and the filter encoding that carries the whole test is inline in the
    # header precisely so it can be reached without either half.
    #
    # ${PLATFORM_FILE_DIALOG_SOURCE} is here to be COMPILED, which is the argument
    # tests/portable-sources.txt already makes for src/render_qt.cpp: an opt-in
    # target's absence is silent, so a Qt backend that no target compiles is a file
    # nothing off Windows ever reads. render_api_tests carries render_qt.cpp for
    # exactly this reason and asserts none of it either. Without this line
    # src/file_dialog_qt.cpp could not honestly appear in the manifest, and the one
    # instrument that would notice it breaking off Windows would be a human running
    # g++ by hand -- which is what happened this pass and is not a standing check.
    #
    # WHAT THAT COSTS, stated because the first version of this block claimed it and
    # then this line took it away: with a backend on the link line, this target is no
    # longer the standing check that the header stays free of BOTH backends. It still
    # catches the cross-platform case -- a Qt include in the header breaks the Windows
    # build, a windows.h dependency breaks the Linux one, because neither platform has
    # the other's headers here. What it no longer catches is the header acquiring a
    # dependency on its OWN platform's backend at both sites at once. The manifest
    # hole is the failure this tree has actually been bitten by; that one is
    # hypothetical, so this is the side to err on.
    add_executable(file_dialog_tests
        tests/file_dialog_tests.cpp
        ${PLATFORM_FILE_DIALOG_SOURCE}
    )
    target_include_directories(file_dialog_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(file_dialog_tests PRIVATE ${PLATFORM_FILE_DIALOG_LIBS})
    if(WIN32)
        target_compile_definitions(file_dialog_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME file_dialog COMMAND file_dialog_tests)
    if(NOT WIN32)
        # Same reasoning as keyboard and qt_text: this binary links Qt Widgets, and a
        # Qt binary with no QT_QPA_PLATFORM aborts in WSL trying to reach a display.
        # Nothing here constructs a QApplication, so it would very likely survive
        # without the variable -- set anyway, because "very likely" is not a reading.
        set_tests_properties(file_dialog PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # include/clipboard.h -- the clipboard seam. Same shape as file_dialog_tests above
    # and for the same two reasons: the pure part of the header is what can be
    # asserted, and the backend is on the link line to be COMPILED so that
    # src/clipboard_qt.cpp can honestly appear in tests/portable-sources.txt.
    #
    # UNLIKE file_dialog_tests, THIS ONE ACTUALLY CALLS ITS BACKEND. A test cannot click
    # a file dialog, but it can put text on a clipboard and read it back, so this target
    # measures the one property the seam refuses to promise -- whether the platform
    # normalizes a CRLF on the way through. It is reported as a MEASUREMENT and not
    # asserted, because asserting it would freeze one platform's behaviour into the
    # contract, and the two platforms are allowed to differ here.
    #
    # It also SAVES AND RESTORES the clipboard, which matters more than it sounds: this
    # runs on a developer's machine under ctest, and a test that leaves a sentinel
    # behind has destroyed whatever they had copied, invisibly, until they next paste.
    add_executable(clipboard_tests
        tests/clipboard_tests.cpp
        ${PLATFORM_CLIPBOARD_SOURCE}
    )
    target_include_directories(clipboard_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(clipboard_tests PRIVATE ${PLATFORM_CLIPBOARD_LIBS})
    # The test constructs a QGuiApplication under this define, because QClipboard is
    # null without one and the round trip would otherwise report "refused" on every
    # run -- a skip that looks like a platform limitation and is actually a missing
    # object. The transformation checks do NOT need it and run either way. Every
    # platform since 2026-10-07; on Windows it runs under the real windows QPA plugin,
    # so the round trip crosses the system clipboard there.
    target_compile_definitions(clipboard_tests PRIVATE TINTA_CLIPBOARD_BACKEND_QT)
    if(WIN32)
        target_compile_definitions(clipboard_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME clipboard COMMAND clipboard_tests)
    add_test(NAME clipboard_control COMMAND clipboard_tests --control)
    if(NOT WIN32)
        set_tests_properties(clipboard clipboard_control PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # include/platform_process.h + include/win32_command_line.h -- the process seam.
    #
    # THIS TARGET IS ITS OWN FIXTURE. Every other seam test here either asserts the pure
    # part of a header or round-trips through a system service; this one needs a CHILD
    # PROCESS, and the only executable guaranteed to exist wherever ctest runs is the
    # test binary itself. So it re-runs itself with --dump-args / --child-exit /
    # --child-sleep, which is why there is no fixture program beside it and why the
    # argument-survival check is the same on both platforms.
    #
    # UNLIKE file_dialog_tests, THE BACKEND IS NOT HERE MERELY TO BE COMPILED. It is
    # called, and its two failure modes -- a refused launch and an overrun -- are each
    # provoked deliberately rather than reasoned about, because those are the two the
    # caller in src/pandoc.cpp distinguishes and the two a stub would silently collapse.
    #
    # shell32 ON WINDOWS ONLY, and it is the TEST's dependency rather than the seam's:
    # ${PLATFORM_PROCESS_LIBS} is empty on Windows because the seam is all kernel32. The
    # test adds CommandLineToArgvW so the quoter is checked against the OS's own parser
    # instead of a second copy of the same rule -- which would agree with a shared
    # misreading and prove nothing.
    add_executable(process_tests
        tests/process_tests.cpp
        ${PLATFORM_PROCESS_SOURCE}
    )
    target_include_directories(process_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(WIN32)
        target_link_libraries(process_tests PRIVATE ${PLATFORM_PROCESS_LIBS} shell32)
        target_compile_definitions(process_tests PRIVATE UNICODE _UNICODE)
    else()
        target_link_libraries(process_tests PRIVATE ${PLATFORM_PROCESS_LIBS})
    endif()
    add_test(NAME process COMMAND process_tests)
    add_test(NAME process_control COMMAND process_tests --control)
    if(NOT WIN32)
        # QCoreApplication, not QGuiApplication -- but the variable is set anyway for the
        # reason file_dialog gives: nothing here constructs an application object, so it
        # would very likely survive without it, and "very likely" is not a reading.
        set_tests_properties(process process_control PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # Qt 6 text layout -- the contract the DirectWrite text code has to be ported
    # onto, measured before any of that port is written. OPT-IN: find_package is
    # QUIET and the target only appears where Qt 6 is installed, so a machine
    # without it (every Windows host here, as of 2026-09-11) configures exactly as
    # before rather than failing.
    #
    # QT_QPA_PLATFORM=offscreen is set on the test and is NOT interchangeable with
    # "minimal". Measured: minimal reports ZERO font families and resolves any
    # request to an empty family, yet QTextLayout still returns numbers and exits 0
    # -- fabricated metrics, whole-number ascents and a descent of exactly 0.000.
    # Running with no QT_QPA_PLATFORM at all ABORTS (exit 134) in WSL, because the
    # default xcb plugin cannot reach a display. The test itself refuses to run
    # unless a font database exists, so this variable going missing fails loudly
    # rather than silently measuring nothing.
    # Svg and Network are OPTIONAL_COMPONENTS, deliberately, and not appended to the
    # required list. The image seam (src/image_qt.cpp) is the first thing in this port
    # to need a Qt module beyond Core and Gui, and adding them as required components
    # would make Qt6_FOUND false on a machine missing either -- which would silently
    # delete qt_text_tests, render_api_tests and every other target in the Qt6_FOUND
    # block below. A green ctest run with a smaller test set is the one failure this
    # tree has caught itself in twice, so the new dependency is not allowed to gate
    # the old targets. Qt6Svg_FOUND / Qt6Network_FOUND carry the answer for the one
    # target that needs it.
    find_package(Qt6 QUIET COMPONENTS Core Gui OPTIONAL_COMPONENTS Svg Network)

    # --- The modifier-key seam, tested on BOTH backends -----------------------
    #
    # It sits here, after find_package, because the backend links Qt. Since 2026-10-07
    # ${PLATFORM_KEYBOARD_SOURCE} is keyboard_qt.cpp on every platform (keyboard_win32.cpp
    # and its GetKeyState were deleted), so this target asserts the one backend there is.
    # The pure predicate in the header (modifierHeld, GetKeyState's bit layout) is still
    # asserted and no longer has a production caller. See the test's own opening comment
    # for what it can and cannot prove; the honest answer is mixed.
    if(WIN32 OR Qt6_FOUND)
        add_executable(keyboard_tests tests/keyboard_tests.cpp ${PLATFORM_KEYBOARD_SOURCE})
        target_include_directories(keyboard_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(keyboard_tests PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME keyboard COMMAND keyboard_tests)
        if(NOT WIN32)
            # Same reasoning as qt_text: a Qt binary with no QT_QPA_PLATFORM ABORTS in
            # WSL trying to reach a display. This test constructs no QGuiApplication,
            # so it would very likely survive without the variable -- it is set anyway,
            # because "very likely" is not a reading and because the two Qt-linked test
            # targets disagreeing about their environment is a trap for whoever changes
            # one of them next.
            set_tests_properties(keyboard PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endif()

    # --- The image seam, tested on BOTH backends ------------------------------
    #
    # Same both-backends shape as keyboard above, and for the same reason: two real
    # implementations, so asserting only one would make this indirection rather than
    # abstraction. ${PLATFORM_IMAGE_SOURCE} resolves to image_win32.cpp or
    # image_qt.cpp, and the assertions are on the artifact both produce -- tightly
    # packed premultiplied BGRA -- not on either backend's API.
    #
    # The guard is narrower than keyboard's on purpose. Off Windows this needs Qt Svg
    # AND Network on top of Core and Gui, so a machine with a partial Qt gets no image
    # target rather than a configure error. That is a hole the manifest cannot see, and
    # it is why the test prints its assertion count: a silently absent target and a
    # passing one are otherwise indistinguishable.
    # THE RENDER BACKEND JOINED THIS TARGET, and it is a link fact rather than a
    # taste call. tinta::decodeImageFileToBitmap -- the entry point render.cpp's
    # local-image branch now calls -- constructs a backend bitmap and asserts the
    # target it was handed belongs to the same backend. Both of those are out-of-line
    # facts about a class, not calls to a function: `typeinfo for QtRenderTarget`
    # comes from the dynamic_cast and `vtable for QtBitmap` from the construction,
    # and each is emitted only in the translation unit holding that class's key
    # function. That is src/render_qt.cpp. An image backend which produces backend
    # bitmaps depends on the backend; there is no arrangement of this target that
    # avoids it.
    #
    # text_qt.cpp travels with it for the same mechanical reason, measured not
    # assumed: render_qt.cpp:238 and :255 dynamic_cast to QtTextFormat and
    # QtTextLayout, whose typeinfo lives there. It is the pair every other Qt target
    # in this file lists, and now it is clear why the pair is not a convention.
    #
    # The Windows half reaches the same shape through ${LEGACY_D2D_SOURCES} rather
    # than naming render_d2d.cpp and text_d2d.cpp, and that indirection is the point:
    # configure with -DTINTA_LEGACY_D2D=OFF and this target joins the link-error
    # inventory of what still depends on the stack being replaced, instead of quietly
    # building a backend the flag says is excluded.
    if(Qt6_FOUND AND Qt6Svg_FOUND AND Qt6Network_FOUND)
        add_executable(image_tests tests/image_tests.cpp ${PLATFORM_IMAGE_SOURCE})
        target_include_directories(image_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        # The Windows arm that stood here compiled image_win32.cpp (WIC) and the Direct2D
        # backends; since 2026-10-07 this test asserts the Qt image backend qinta uses on
        # every platform.
        target_sources(image_tests PRIVATE src/render_qt.cpp src/text_qt.cpp)
        target_link_libraries(image_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Svg Qt6::Network)
        if(WIN32)
            target_compile_definitions(image_tests PRIVATE UNICODE _UNICODE)
            target_sources(image_tests PRIVATE src/text_utils.cpp ${PLATFORM_PATHS_SOURCE})
            target_link_libraries(image_tests PRIVATE shell32)
        endif()
        add_test(NAME image COMMAND image_tests)
        if(NOT WIN32)
            # QImage and QSvgRenderer both work headless, but QPainter on a QImage is
            # raster-backed and the offscreen plugin is what keeps a Qt binary from
            # aborting in WSL where no display exists. Set for the same reason as
            # keyboard's: two Qt-linked targets disagreeing about their environment is
            # a trap for whoever edits one of them next.
            set_tests_properties(image PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endif()

    if(Qt6_FOUND)
        add_executable(qt_text_tests tests/qt_text_tests.cpp)
        target_link_libraries(qt_text_tests PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME qt_text COMMAND qt_text_tests)

        # THE QT TARGETS MUST USE THE DYNAMIC CRT. The tree-wide setting at the top of
        # this file was the static one until 2026-10-07, which is why this override
        # exists; it is now the default as well, and the override restates it.
        #
        # MEASURED 2026-09-16, after both Qt tests on Windows put up a modal
        # "Debug Assertion Failed! ... __acrt_first_block == header" from
        # minkernel/crts/ucrt/src/appcrt/heap/debug_heap.cpp. dumpbin -dependents on
        # qt_text_tests.exe named KERNEL32, Qt6Cored and Qt6Guid and NO CRT dll at all
        # -- static CRT -- while Qt6Cored.dll names ucrtbased.dll, MSVCP140D.dll and
        # VCRUNTIME140D.dll. Two debug heaps in one process, so any block Qt allocates
        # and we free (or the reverse) trips that assertion. Nothing to do with the
        # seam under test.
        #
        set_property(TARGET qt_text_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

        # THE PLATFORM PLUGIN IS NOT THE SAME CHOICE ON BOTH SYSTEMS, and this was
        # measured on 2026-09-15 rather than reasoned about. Off Windows, `offscreen`
        # carries a real font database and `minimal` does not, which is why every
        # comment in this tree says offscreen and never minimal. ON WINDOWS, offscreen
        # has NO font database at all: qt_text_tests refuses with exit 2 under it, and
        # render_api_tests refuses too -- both by the guards written for exactly this,
        # which is the guards working rather than a new problem.
        #
        # Under `windows` the database is real and render_api passes all 266 assertions,
        # the same count and the same zero failures as Qt on Linux and Direct2D here.
        # So the variable is pinned per platform, not once.
        #
        # THAT WINDOWS READING WAS NOT CLEAN, and this is the correction rather than a
        # deletion. It was taken with a modal CRT assertion dialog on screen, which
        # somebody dismissed with Ignore -- so the process finished, printed 266 and
        # exited 0, and ctest recorded a pass. A dialog that has to be clicked away is
        # not a green run, and on 2026-09-16 the same assertion took qt_text down as an
        # abort while render_api's Ignore let it through: the identical defect reported
        # once as a pass and once as a failure. The cause and the fix are at the
        # MSVC_RUNTIME_LIBRARY override below. The 266 stands -- Linux agrees, and the
        # cross-heap free is at teardown -- but it stands as a number taken through a
        # broken instrument, and the run that re-takes it is the one to cite.
        set(TINTA_QPA_PLATFORM "offscreen")
        if(WIN32)
            set(TINTA_QPA_PLATFORM "windows")
        endif()

        set_tests_properties(qt_text PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")

        # The drawing seam and its first non-Direct2D backend. This target is also
        # the only thing that compiles include/render_api.h off Windows: a header
        # appears in no object list, so tests/portable-sources.txt is structurally
        # blind to it and the portable build would read none of it.
        #
        # tests/render_api_shared.cpp holds the assertions and is compiled into BOTH
        # this target and render_d2d_tests below, so the two backends are held to the
        # same assertions in the same order. Editing it changes both readings at once,
        # which is the intent: a backend cannot be made green on its own.
        add_executable(render_api_tests
            tests/render_api_tests.cpp
            tests/render_api_shared.cpp
            src/render_qt.cpp
            src/text_qt.cpp)
        target_include_directories(render_api_tests PRIVATE include)
        target_link_libraries(render_api_tests PRIVATE Qt6::Core Qt6::Gui)
        # THE IMAGE-SEAM FAMILIES, AND WHY THEY ARE CONDITIONAL WHEN THIS TARGET IS NOT.
        # tinta::decodeIconBytesToBitmap and tinta::rasterizeToImage both need a live
        # RenderTarget, which the harnesses here have and tests/image_tests.cpp does not,
        # so this is where those assertions can run on both backends. But src/image_qt.cpp
        # also holds rasterizeSvg and fetchToTempFile, so linking it drags in Qt Svg AND
        # Network -- a narrower guard than this target's own Qt6_FOUND, and exactly the
        # partial-Qt hole image_tests is guarded against. Widening this target's
        # requirement to match would trade a skipped family for a target that does not
        # build at all. TINTA_HAVE_IMAGE_SEAM is how render_api_shared.cpp knows which
        # happened, and each #else arm PRINTS the skip -- an arm never compiled and an
        # arm that broke read the same otherwise.
        #
        # THE DEFINE WAS CALLED TINTA_HAVE_ICON_SEAM UNTIL THE OFFSCREEN-RASTER FAMILY
        # LANDED, and it is renamed rather than joined by a second one. The condition for
        # rasterizeToImage/encodePng is not merely similar to the icon family's, it is the
        # SAME condition -- both live in src/image_qt.cpp and src/image_win32.cpp -- and
        # two defines set from one `if` are two things that can be edited apart. One
        # correctly-named guard cannot drift from itself.
        if(Qt6Svg_FOUND AND Qt6Network_FOUND)
            target_sources(render_api_tests PRIVATE
                src/image_qt.cpp ${TINTA_ICON_GENERATED})
            target_link_libraries(render_api_tests PRIVATE Qt6::Svg Qt6::Network)
            target_compile_definitions(render_api_tests PRIVATE TINTA_HAVE_IMAGE_SEAM)
        endif()
        add_test(NAME render_api COMMAND render_api_tests)
        # Dynamic CRT, same reason and same measurement as qt_text_tests above.
        set_property(TARGET render_api_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        # This one paints into a QImage and draws no text, so it does not depend on
        # a font database the way qt_text does. offscreen is set anyway: the arm
        # that matters is that no arm reaches for a real display, and pinning it
        # keeps the two Qt targets from disagreeing about their environment.
        set_tests_properties(render_api PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")

        # The PDF seam's Qt arm, and the same two-backend shape one seam up.
        # tests/pdf_export_shared.cpp holds the assertions and is compiled into BOTH this
        # target and pdf_export_win32_tests below, so neither arm can be made green on its
        # own -- and every assertion reads the PDF's own BYTES, because the interesting
        # failures of a PDF backend all return true.
        #
        # THIS TARGET IS WHY THE QT ARM IS NOT AN if(WIN32)/else() SELECTION. A seam file
        # chosen by an else() is compiled only off Windows, so on the host that builds,
        # tests and commits it would be compiled by no configuration at all -- which this
        # tree has already paid for once. A target that builds HERE is what makes the arm
        # exercised, and the selection can arrive later with the caller that needs it.
        #
        # Qt6::Gui is the whole link cost: QPdfWriter is in QtGui, not Qt6::PrintSupport,
        # measured in this Qt's own include tree. src/text_qt.cpp travels with
        # src/render_qt.cpp because QtRenderTarget's text methods name its types -- the
        # same pairing render_api_tests carries, for the same link-time reason.
        add_executable(pdf_export_qt_tests
            tests/pdf_export_qt_tests.cpp
            tests/pdf_export_shared.cpp
            src/pdf_export_qt.cpp
            src/render_qt.cpp
            src/text_qt.cpp)
        target_include_directories(pdf_export_qt_tests PRIVATE include)
        target_link_libraries(pdf_export_qt_tests PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME pdf_export_qt COMMAND pdf_export_qt_tests)
        # Dynamic CRT, same reason and same measurement as the Qt targets above.
        set_property(TARGET pdf_export_qt_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set_tests_properties(pdf_export_qt PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")

        # --- The Qt entry point: the first test that paints a tinta WINDOW --------
        #
        # Every other drawing assertion in this file paints into a BITMAP. That is
        # right for the seam's own tests and it leaves the gap where the port is
        # riskiest: between a QWidget's paintEvent and the abstract render target the
        # tree draws through. This target closes it by constructing the same
        # QtAppWindow src/main_qt.cpp runs, grabbing it, and reading the pixels.
        #
        # It links the same measured closure as qinta above, minus main_qt.cpp --
        # so a change that breaks the shell breaks this test, which is the point of
        # sharing the window class rather than reimplementing it in the test.
        #
        # "The same closure" is now literally the same CMake variable
        # (QINTA_APP_SOURCES, defined beside qinta) rather than a second list I
        # promised to keep in step. It stopped being a promise worth making when the
        # closure went from 8 files to 28.
        #
        # AND THIS IS NOW THE TEST THAT LINKS src/render.cpp. Until this commit no test
        # on EITHER platform did -- the widest compile/run gap in the tree, and the
        # honest limit printed at the end of README's render.cpp row for several spans.
        # A file that compiles and links but is never executed has been asserted to
        # nothing; what closes that is below, where the test parses a document, lays it
        # out through render.cpp and counts the pixels that changed.
        #
        # Widgets, not just Core and Gui: QtAppWindow is a QWidget and the test needs
        # QApplication. The guard is therefore narrower than the enclosing
        # Qt6_FOUND, for the same reason image_tests' is -- a partial Qt install
        # should lose this target and SAY so, not fail the configure.
        if(Qt6Widgets_FOUND)
            # A test built on qinta's own app sources and Qt backends -- what the six tests that
            # used to take their App from the Direct2D stack (initD2D) are built as since that
            # stack was retired on 2026-10-06. The same libraries, definitions and runtime as
            # qt_entry_tests below, so a test and the app cannot be built differently. They run
            # on every host now, not only Windows. The QT_QPA_PLATFORM pin is per platform for
            # the reason TINTA_QPA_PLATFORM gives, and on Windows the Qt bin directory joins
            # PATH through the all-tests rule at the end of this file.
            function(tinta_qt_app_test target test_name test_source)
                add_executable(${target} ${test_source} ${QINTA_APP_SOURCES})
                target_include_directories(${target} PRIVATE include ${md4c_SOURCE_DIR}/src)
                target_link_libraries(${target} PRIVATE
                    md4c Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Qt6::PrintSupport)
                target_compile_definitions(${target} PRIVATE TINTA_QT_UI
                    ${TINTA_VERSION_DEFINITIONS} ${ARGN})
                if(WIN32)
                    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
                    target_link_libraries(${target} PRIVATE shell32 dwmapi)
                    target_sources(${target} PRIVATE src/window_chrome_win32.cpp)
                endif()
                set_property(TARGET ${target} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
                # test_name NONE: the caller registers its own test (the theme test runs through
                # the isolated portable-folder wrapper rather than directly).
                if(NOT test_name STREQUAL "NONE")
                    add_test(NAME ${test_name} COMMAND ${target})
                    set_tests_properties(${test_name} PROPERTIES
                        ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")
                endif()
            endfunction()

            tinta_qt_app_test(code_block_layout_tests code_block_layout
                tests/code_block_layout_tests.cpp
                TINTA_CODE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/code-block-spacing.md")
            tinta_qt_app_test(blockquote_layout_tests blockquote_layout
                tests/blockquote_layout_tests.cpp
                TINTA_QUOTE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blockquote-spacing.md")
            tinta_qt_app_test(html_layout_tests html_layout
                tests/html_layout_tests.cpp
                TINTA_HTML_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/html-align-links.md")
            tinta_qt_app_test(input_tests input_shortcuts tests/input_tests.cpp
                TINTA_SHORTCUT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shortcuts-layouts.md")
            tinta_qt_app_test(file_path_tests copy_file_path tests/file_path_tests.cpp)
            tinta_qt_app_test(draft_owner_tests draft_owner tests/draft_owner_tests.cpp)
            if(WIN32)
                # /register, run for real against a redirected HKCU (see the test's header).
                # Windows-only by source: src/settings_win32.cpp is the registry code itself.
                tinta_qt_app_test(file_association_tests file_association
                    tests/file_association_tests.cpp)
                target_sources(file_association_tests PRIVATE src/settings_win32.cpp)
            endif()
            tinta_qt_app_test(math_layout_tests math_layout tests/math_layout_tests.cpp
                TINTA_MATH_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/math-compatibility.md")
            tinta_qt_app_test(theme_tests NONE tests/theme_tests.cpp
                TINTA_THEME_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inline-code-color.md")
            add_test(NAME inline_code_theme COMMAND ${CMAKE_COMMAND}
                -DTEST_BINARY=$<TARGET_FILE:theme_tests>
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/theme-test-$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_test.cmake)
            set_tests_properties(inline_code_theme PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")

            add_executable(qt_entry_tests
                tests/qt_entry_tests.cpp
                # ISC-17's Win32 half: EnumDisplayMonitors/GetMonitorInfoW, the enumeration
                # src/main_d2d.cpp places windows over, so run 20 can compare it against
                # tinta::monitorsFromQt(). Its own TU because <windows.h>'s macro set is hostile
                # to anything included after it and qt_entry_tests.cpp is 2,000 lines of Qt. It
                # is in NO other target and NOT in ${QINTA_APP_SOURCES}: it is a test instrument,
                # and compiling it into the app would ship a second monitor enumeration.
                tests/win32_monitor_dump.cpp
                # Slice 13's DWM readback: a test instrument for the same reason, and in its own
                # TU for the same reason.
                tests/win32_chrome_readback.cpp
                ${QINTA_APP_SOURCES}
            )
            target_include_directories(qt_entry_tests PRIVATE
                include ${md4c_SOURCE_DIR}/src)
            # Svg and Network arrive with the closure, not with the test: image_qt.cpp's
            # QSvgRenderer and QNetworkAccessManager carry fourteen of the twenty
            # residual symbols the fixpoint left. Same two lines as qinta, for the
            # same measured reason.
            target_link_libraries(qt_entry_tests PRIVATE
                md4c Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Qt6::PrintSupport)
            # Where tests/fixtures/ is, for the QPrinter block's CJK fixture (2026-10-06): the
            # test reads the CHECKED-IN fixture rather than a copy, so editing the fixture is
            # editing what is printed.
            target_compile_definitions(qt_entry_tests PRIVATE
                TINTA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
            # Same stack as qinta, so the same guard answer -- see TINTA_QT_UI at qinta. The
            # version too, because startpage.cpp and overlays.cpp arrive with the key router's
            # closure and both print it; qinta has carried the same line since it had a window.
            target_compile_definitions(qt_entry_tests PRIVATE TINTA_QT_UI
                ${TINTA_VERSION_DEFINITIONS})
            if(WIN32)
                target_compile_definitions(qt_entry_tests PRIVATE UNICODE _UNICODE)
                target_link_libraries(qt_entry_tests PRIVATE shell32)
                # Same as qinta: the chrome sink paints through DWM on this host.
                target_sources(qt_entry_tests PRIVATE src/window_chrome_win32.cpp)
                target_link_libraries(qt_entry_tests PRIVATE dwmapi)
            endif()
            add_test(NAME qt_entry COMMAND qt_entry_tests)
            # Dynamic CRT, same reason and same measurement as qt_text_tests above.
            set_property(TARGET qt_entry_tests PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            # This one DOES need a font database -- it asserts that glyph pixels
            # appeared -- so the per-platform pin matters here as much as it does for
            # qt_text, and the test refuses with exit 2 rather than reporting a blank
            # band as a broken seam.
            set_tests_properties(qt_entry PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")
            # qinta's four harness switches, run as the real binary (2026-10-06). A script and not
            # a C++ test because the subject is main(), which no test can link: the probe's launch
            # block reaches it on Linux only, and this reaches it on all three CI hosts. It reads
            # each written file, and its two controls are the output guard and a missing document.
            if(TARGET qinta)
                add_test(NAME qinta_cli COMMAND ${CMAKE_COMMAND}
                    -DQINTA_EXE=$<TARGET_FILE:qinta>
                    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/qinta_cli
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qinta_cli.cmake)
                set_tests_properties(qinta_cli PROPERTIES
                    ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")
            endif()
        else()
            message(STATUS "Qt6 Widgets not found -- qt_entry test skipped")
        endif()

        # THE QT DRAG GHOST, READ PIXEL BY PIXEL. The risk this target exists for is a Qt arm that
        # compiles, links, shows something card-shaped, and draws a SUBTLY different card from the
        # Windows one -- a soft corner instead of a hard one, the border colour where the fill
        # belongs, or a surface that is transparent everywhere because the paint silently did
        # nothing. None of that shows up in a build and all of it shows up here.
        #
        # Widgets rather than Core+Gui, because src/tab_ghost_qt.cpp defines TabGhostWidget. The
        # test itself never constructs one -- it asserts over a plain QImage -- but the TU needs the
        # library, and a partial Qt install should lose this one target and say so.
        #
        # Deliberately NOT linked against ${QINTA_APP_SOURCES}: two files and a QImage are the whole
        # closure. That is the seam paying for itself twice -- the card is assertable without the
        # application, which is the same property that made the Win32 arm testable.
        if(Qt6Widgets_FOUND)
            add_executable(tab_ghost_qt_tests
                tests/tab_ghost_qt_tests.cpp
                src/tab_ghost.cpp
                src/tab_ghost_qt.cpp
            )
            target_include_directories(tab_ghost_qt_tests PRIVATE include)
            target_link_libraries(tab_ghost_qt_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
            add_test(NAME tab_ghost_qt COMMAND tab_ghost_qt_tests)
            # Dynamic CRT, same reason and same measurement as qt_text_tests and qt_entry_tests.
            set_property(TARGET tab_ghost_qt_tests PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            # THIS ONE NEEDS A REAL FONT DATABASE -- it counts the pixels a title draws -- so the
            # per-platform pin is load-bearing here, not boilerplate. Under `offscreen` on Windows
            # the database is empty, the card comes back textless, and the failure would name the
            # seam instead of the plugin. The test refuses with exit 2 rather than reporting a blank
            # card as a broken arm.
            set_tests_properties(tab_ghost_qt PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=${TINTA_QPA_PLATFORM}")
        else()
            message(STATUS "Qt6 Widgets not found -- tab_ghost_qt test skipped")
        endif()

        # WINDOWS ONLY, AND MEASURED RATHER THAN ANTICIPATED. The first time these two
        # targets ran on Windows -- 2026-09-15, the day Qt 6.8.3 was installed here --
        # both FAILED under ctest while both PASSED when launched by hand with
        # C:/Qt/.../bin on PATH. The cause is the Windows DLL search: Qt6Core.dll and
        # Qt6Gui.dll live in the Qt prefix, are not beside the .exe, and there is no
        # rpath equivalent, so ctest's child could not start at all.
        #
        # Worth the paragraph because of what it LOOKED like: a process that dies before
        # main() prints no assertion count, and ctest reports it the same way it reports
        # a real assertion failure -- ***Failed, a duration, no output. Nothing in the
        # line distinguishes "the DLL was missing" from "the seam is broken", which is
        # the failure mode this tree keeps finding in its own instruments.
        #
        # Off Windows this is not needed and not set: the Linux build finds Qt through
        # the loader's own configuration, which is why 23 tests were green there while
        # these two were red here.
        #
        # ENVIRONMENT_MODIFICATION rather than ENVIRONMENT because PATH must be
        # PREPENDED to what the runner already has, not replaced by it. It needs CMake
        # 3.22 and this file only requires 3.15, so it is guarded -- an older CMake gets
        # no property and the same by-hand PATH it needed before.
        # The PATH itself is set for EVERY test at the end of this file. This block used to
        # keep a hand-written list of the Qt tests, and missing one from it was paid for
        # twice (pdf_export_qt, then tab_ghost_qt): the loader's "Qt6Core.dll was not found"
        # box is MODAL, so the reading was a 330-second ***Failed with no output and a
        # dialog on somebody's desktop. Once the clipboard, keyboard, paths and process
        # seams collapsed onto Qt (2026-10-07), nearly every test links Qt on Windows too,
        # so the list became the wrong shape: one rule over all tests cannot miss one.

        message(STATUS "Qt6 ${Qt6_VERSION} found -- qt_text and render_api targets enabled")
    else()
        message(STATUS "Qt6 not found -- qt_text and render_api targets skipped (this is expected on Windows)")
    endif()
    # That opt-in used to carry a KNOWN WEAKNESS: its ABSENCE was silent, so
    # uninstalling Qt left the probe reporting PASS with one fewer test -- the same
    # under-reporting shape that made an earlier version of
    # scripts/probe-portable-linux.sh green while it watched 5 of 22 files.
    #
    # CLOSED, without giving up the opt-in: src/render_qt.cpp is now listed in
    # tests/portable-sources.txt, which the probe diffs against the build's real
    # object set in BOTH directions. Off Windows, a missing Qt therefore fails as
    # listed-but-not-compiled instead of passing quietly. The opt-in still does its
    # only job -- letting Windows, which has no Qt, configure -- and Qt is now a
    # hard requirement for the non-Windows build, which is what choosing it means.

    # --- Windows-only targets: these link Direct2D/DirectWrite/shell32.
    #
    # inline_style_tests used to head this section and has moved out: it linked the
    # two Direct2D backend sources only for App's vtables, which App no longer needs.
    # config_path_tests below is Windows-only for a reason that has not expired --
    # see its own comment.
    if(WIN32)
    # WINDOWS-ONLY BY SUBJECT, not by leftover link dependency. It checks how
    # configFilePathForShell copes with MSIX filesystem virtualization, and
    # tests/config_path_tests.cpp includes <windows.h> for CreateFileW, HANDLE and
    # GetTickCount64 to do it. src/config_dir.cpp is in the portable manifest and is
    # compiled off Windows by other targets, so this gate costs no portable coverage
    # of the file -- only of the MSIX behaviour, which has no non-Windows meaning.
    add_executable(config_path_tests
        tests/config_path_tests.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(config_path_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(config_path_tests PRIVATE ${PLATFORM_PATHS_LIBS})
    target_compile_definitions(config_path_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME config_paths COMMAND config_path_tests)
    endif()  # WIN32

# The README figure gate, as two ctest arms. OUTSIDE the WIN32 block on purpose: the
# script reads the source tree and needs no build, so it is correct on both platforms
# and the Linux suite gaining it is the point rather than a side effect.
#
# scripts/check-readme-figures.sh re-derives eighteen figures README.md publishes about
# the port and asserts the README actually says them. It is the ONLY instrument in this
# repo that reads prose -- the portable probe checks the manifest, ctest checks the tests,
# the compiler checks the code -- and until now it ran when someone remembered to type it.
# Its positive control ran only when someone remembered to type a FLAG, which is a weaker
# thing again, and a wrapper in this project's own scratch rig has already been caught
# dropping exactly that flag and reporting the subject's pass twice.
#
# TWO ARMS FOR THE SAME REASON clipboard/clipboard_control ARE TWO. The control is what
# says the eighteen assertions are anchored to their own sentences rather than matching
# some other number in a document made of numbers -- the script's first version matched
# bare numbers and its control found 6 of 39 claims still matching when perturbed. A green
# subject arm with no control arm beside it proves that grep found something.
#
# EXPECTED_CHECKS is a second reading and not belt-and-braces over the exit status; see
# tests/run_figure_gate.cmake for why the exit status cannot say how many claims ran.
# Move the number when a claim is added to the script -- the driver fails in both
# directions and names which direction it was.
# HINTS ahead of PATH because C:\Windows\System32\bash.exe is the WSL launcher and exists
# on this host: winning the search there would run the gate under a Linux rootfs against a
# Windows working directory. That failure is loud rather than silent -- the script exits 90
# saying it cannot find README.md -- but preferring Git's bash means not relying on PATH
# order to avoid it. Off Windows the hints simply do not exist and PATH answers.
find_program(BASH_EXECUTABLE NAMES bash
    HINTS "$ENV{ProgramFiles}/Git/usr/bin" "$ENV{ProgramFiles}/Git/bin")
if(BASH_EXECUTABLE)
    add_test(NAME readme_figures COMMAND ${CMAKE_COMMAND}
        -DBASH_EXE=${BASH_EXECUTABLE}
        -DGATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-readme-figures.sh
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DEXPECTED_CHECKS=20
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_figure_gate.cmake)
    add_test(NAME readme_figures_control COMMAND ${CMAKE_COMMAND}
        -DBASH_EXE=${BASH_EXECUTABLE}
        -DGATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-readme-figures.sh
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DEXPECTED_CHECKS=20
        -DGATE_CONTROL=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_figure_gate.cmake)
    # LABELLED because these two are the slowest arms in the suite by more than an order of
    # magnitude, and the comparison is against the WHOLE of the rest rather than against its
    # slowest member: `ctest -L prose` reads 48.9 s + 46.2 s = 95.2 s for the two, while
    # `ctest -LE prose` reads 2.8 s for all thirty-nine others combined. So EITHER arm alone
    # costs about seventeen times the entire pre-existing suite, and the pair is what turns a
    # three-second `ctest` into a ninety-five-second one.
    # Stated as a range rather than to two decimals on purpose -- across runs each arm lands
    # between 37 s and 49 s, so a precise figure here would be false precision.
    # BOTH READINGS ARE WARM RUNS, AND MIXING WARM WITH COLD IS HOW THIS COMMENT WAS WRONG
    # THE FIRST TIME. The first `ctest` after a link pays an antivirus scan on every freshly
    # built .exe: the same thirty-nine tests that sum to 2.8 s warm summed to 66.6 s in the
    # run straight after a rebuild, which is 20x, and blockquote_layout alone read 11.5 s
    # there against 0.1 s warm. Neither number is wrong; they answer different questions, and
    # a cold prose figure divided by a warm suite figure answers neither.
    # The cost of the arms themselves is process spawns -- the script shells out to rg and
    # grep a few hundred times and Windows charges for every one -- not anything it could
    # compute faster, which is why they do NOT get cheaper on a warm run the way the
    # compiled tests do.
    # `ctest -LE prose` gets the old suite back, and the label exists so that the answer
    # to "the suite got slow" is a documented flag rather than someone deleting the arms.
    set_tests_properties(readme_figures readme_figures_control PROPERTIES LABELS prose)

    # THE PORTABLE-CLAIM GATE, and it is here rather than in the prose label for a measured
    # reason: it is ONE awk pass over all 250 tracked text files and reads about four seconds,
    # against 37-49 s for either readme_figures arm. THE FOUR SECONDS ARE THE THIRD READING AND
    # THE FIRST TWO ARE WORTH THE LINE. This comment said "about two seconds" before the arm had
    # ever run; the first ctest reading was 105 s, which would have made this pair the most
    # expensive family in the suite by a wide margin. The gap was all process spawns on this host
    # -- one awk per file plus a `cut`/`grep` per hit and per allowlist row -- so 68 basenames
    # became one alternation (105 s to 47 s), 250 awk invocations became one (47 s to 26 s), and
    # the two shell assertion loops became two awk passes (26 s to 4 s). Every counter the gate
    # prints was identical at all four timings, which is what says the speed-ups changed the
    # instrument's shape and not its reading.
    # Putting it under `prose` would mean `ctest -LE prose`
    # -- the flag this file recommends two comments up for getting the fast suite back -- silently
    # stops checking it, and the whole point of the gate is to catch a claim nobody re-reads.
    #
    # WHAT IT ASSERTS: no comment anywhere in the tracked tree calls a tests/portable-sources.txt
    # entry unportable, unless tests/portable-claims-allow.txt excuses that exact line text with
    # one of five reasons. WHY IT EXISTS: on 2026-09-22 one slice corrected twelve comments naming
    # the wrong blocker and closed a criterion saying the sweep was complete across six files. Two
    # more survived -- README.md's math_render.cpp sentence and this manifest's own description of
    # src/input.cpp as "344 native mentions deep and not portable" -- because the sweep's phrase
    # set had been derived from the phrasings it had already found in THIS file. A census keyed on
    # the shape you have already fixed is blind to the shape you have not.
    #
    # EXPECTED_MIN_HITS is a FLOOR, not a tracking count, so it does not have to be maintained
    # when a comment is reworded. It exists because the gate's own strongest defence -- every
    # allowlist row must match a live hit, so an empty corpus turns every row stale -- can be
    # defeated by emptying the allowlist in the same edit that breaks the matcher.
    add_test(NAME portable_claims COMMAND ${CMAKE_COMMAND}
        -DBASH_EXE=${BASH_EXECUTABLE}
        -DGATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-portable-claims.sh
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DEXPECTED_MIN_HITS=20
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_claims_gate.cmake)
    add_test(NAME portable_claims_control COMMAND ${CMAKE_COMMAND}
        -DBASH_EXE=${BASH_EXECUTABLE}
        -DGATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-portable-claims.sh
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DEXPECTED_MIN_HITS=20
        -DGATE_CONTROL=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_claims_gate.cmake)
else()
    # Said out loud, because a suite that is quietly two entries shorter is the same class
    # of problem the driver's count assertion exists to catch. The figures in README.md go
    # unchecked on this configuration and the configure log is where a reader finds that out.
    message(STATUS
        "readme_figures: SKIPPED -- no bash found, so the README figure gate is not wired "
        "into ctest on this configuration. Run it by hand: bash scripts/check-readme-figures.sh")
    message(STATUS
        "portable_claims: SKIPPED for the same reason -- nothing checks that the tree's comments "
        "agree with tests/portable-sources.txt on this configuration. Run it by hand: "
        "bash scripts/check-portable-claims.sh")
endif()
endif()

# Every Windows test gets the Qt kit's bin directory prepended to PATH, for the reason
# the Qt block above gives: since 2026-10-07 the platform seams are Qt on every platform,
# so a test binary without the DLLs on PATH cannot start, and on Windows the loader says
# so in a modal box rather than on stderr. ENVIRONMENT_MODIFICATION (CMake 3.22) prepends
# rather than replacing PATH; an older CMake gets no property and needs Qt on PATH by hand.
# Children inherit it, so the cmake -P drivers and qinta_cli are covered too.
if(WIN32 AND Qt6_FOUND AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    get_property(TINTA_ALL_TESTS DIRECTORY PROPERTY TESTS)
    if(TINTA_ALL_TESTS)
        set_tests_properties(${TINTA_ALL_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<SHELL_PATH:$<TARGET_FILE_DIR:Qt6::Core>>")
    endif()
endif()

# Install
if(TARGET qinta)
    install(TARGETS qinta RUNTIME DESTINATION bin)
endif()

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
    # Static CRT: the exe must run on clean Windows installs without the
    # VC++ redistributable (costs ~250 KB, keeps the single-file promise)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
find_package(Qt6 QUIET COMPONENTS Core Gui OPTIONAL_COMPONENTS Widgets)

# The platform path seam. Its POSIX half is GONE: src/platform_paths_posix.cpp
# was replaced by src/platform_paths_qt.cpp, which is written to be correct on all
# three platforms and is the file src/platform_paths_win32.cpp collapses into at
# the TINTA_LEGACY_D2D cutover below. That is a deletion rather than another seam,
# which is what the goal -- one stack everywhere -- asks for.
#
# Nine of the eleven functions have no platform branch in the Qt file. The two that
# do are named there: executableDirectory, because every Qt answer needs a
# QCoreApplication instance and the callers run before one exists, and
# documentPathsEqual, because Qt has no per-platform path-identity rule.
if(WIN32)
    set(PLATFORM_PATHS_SOURCE src/platform_paths_win32.cpp)
else()
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Qt 6 (Core and Gui) is required off Windows: the platform path, keyboard and image "
            "seams all resolve to their Qt implementations there. This is not a new dependency -- "
            "src/keyboard_qt.cpp and src/image_qt.cpp were already the only non-Windows halves -- "
            "it is the first one that fails loudly instead of silently dropping test targets.")
    endif()
    set(PLATFORM_PATHS_SOURCE src/platform_paths_qt.cpp)
    set(PLATFORM_PATHS_BACKEND_QT TRUE)
endif()

# THE SEAM'S LIBRARIES TRAVEL WITH THE SEAM, and this variable exists because they
# did not. Seventeen targets list ${PLATFORM_PATHS_SOURCE}, and twelve of them
# carried a hand-written `if(WIN32) target_link_libraries(x PRIVATE shell32)` block
# -- shell32 being there for exactly one reason: SHGetFolderPathW in the Windows
# half of this seam. The Qt half needs Qt6::Core and Qt6::Gui the same way, and
# adding it without this variable built ONE target (platform_paths_tests, the only
# one edited by hand) and broke the other sixteen at `QByteArray: No such file or
# directory` -- 15 of 25 ctest entries Not Run, which is a far louder failure than
# the previous file, needing no include path at all, could ever have produced.
#
# So the dependency is stated once, beside the file selection it belongs to. A new
# target that lists the source and forgets the libraries still breaks, but it breaks
# the same way for everyone rather than per-platform, and the fix is one token.
if(WIN32)
    set(PLATFORM_PATHS_LIBS shell32)
else()
    set(PLATFORM_PATHS_LIBS Qt6::Core Qt6::Gui)
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
# working implementations, not with the ones that admit to having one.
if(WIN32)
    set(PLATFORM_KEYBOARD_SOURCE src/keyboard_win32.cpp)
else()
    set(PLATFORM_KEYBOARD_SOURCE src/keyboard_qt.cpp)
endif()

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
if(WIN32)
    set(PLATFORM_IMAGE_SOURCE src/image_win32.cpp)
else()
    set(PLATFORM_IMAGE_SOURCE src/image_qt.cpp)
endif()

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
if(WIN32)
    set(PLATFORM_FILE_DIALOG_SOURCE src/file_dialog_win32.cpp)
    set(PLATFORM_FILE_DIALOG_LIBS comdlg32)
else()
    if(NOT Qt6Widgets_FOUND)
        message(FATAL_ERROR
            "Qt 6 Widgets is required off Windows: src/file_dialog_qt.cpp is the only "
            "non-Windows implementation of the include/file_dialog.h seam, and QFileDialog "
            "lives in QtWidgets. On Debian and Ubuntu the package is qt6-base-dev, which "
            "also provides the Core and Gui components already required above.")
    endif()
    set(PLATFORM_FILE_DIALOG_SOURCE src/file_dialog_qt.cpp)
    set(PLATFORM_FILE_DIALOG_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)
endif()

# The clipboard seam (include/clipboard.h): the TEXT half of the system clipboard,
# selected like the four above.
#
# QtGui IS ENOUGH HERE, and the contrast with the seam directly above is the point.
# QClipboard hangs off QGuiApplication, so this seam needs no module the tree did not
# already require. Widgets was the price of a file dialog specifically, not the price
# of having a real second implementation in general -- and reading it as the latter
# would make every future seam look more expensive than it is.
#
# The IMAGE half of the clipboard is NOT part of this seam. src/export_docx.cpp keeps
# CF_DIB, a registered L"PNG" format and a CF_BITMAP read, because none of the three
# has a QClipboard shape that is a translation rather than a rewrite, and that file
# cannot compile off Windows for an unrelated reason anyway. So a build off Windows
# gets the text half and no image half, which is honest: it is what exists.
if(WIN32)
    set(PLATFORM_CLIPBOARD_SOURCE src/clipboard_win32.cpp)
    set(PLATFORM_CLIPBOARD_LIBS "")
else()
    set(PLATFORM_CLIPBOARD_SOURCE src/clipboard_qt.cpp)
    set(PLATFORM_CLIPBOARD_LIBS Qt6::Core Qt6::Gui)
endif()

# The Direct2D and DirectWrite backends are TRANSITIONAL. The goal is one stack
# on every platform, so src/render_d2d.cpp and src/text_d2d.cpp are scheduled for
# DELETION, not for porting -- and neither is src/settings_win32.cpp (becomes
# QSettings) or src/platform_paths_win32.cpp (becomes QStandardPaths).
#
# The option DEFAULTS ON, but BOTH of the reasons originally written here have
# since expired, and they are corrected in place rather than deleted so the
# argument that moved them stays on the record.
#
# It used to say that turning the option off "does not produce a Qt Windows build,
# it produces no Windows build at all". That is now FALSE, and measurably so: with
# -DTINTA_LEGACY_D2D=OFF the qinta target compiles, links with zero LNK2019 and
# produces a binary that launches and stays up (negative-controlled by taking the Qt
# DLLs off PATH, which turns the same binary into a 127). So a Windows build without
# a line of Direct2D in it EXISTS. What fails with the option off is the `tinta`
# target -- the Direct2D app -- and that is a different sentence entirely.
#
# It also used to say that flipping the default "requires Qt on Windows, which is
# not installed here". Also now false: Qt 6.8.3 is installed at
# C:/Qt/6.8.3/msvc2022_64 and configure finds it.
#
# So the reason the default stays ON is no longer a missing dependency, it is
# FEATURE PARITY: qinta renders and scrolls a document but has no input
# handling, no editor and no search, so deleting the backend today would delete the
# working product to keep the more portable one. That is a product call, not a
# build-system one.
#
# The option's other job is unchanged and is why it is worth keeping even after the
# cutover argument is settled: it is a MEASURING INSTRUMENT. Configure with
# -DTINTA_LEGACY_D2D=OFF and the `tinta` link errors are an exact inventory of what
# still depends on the stack being replaced -- currently 57 unresolved externals,
# of which only 24 are real app-code leaks; the other 33 are referenced solely by
# d2d_init.obj, whose entire job is constructing the D2D device and factories, so
# they die WITH the backend instead of blocking it.
option(TINTA_LEGACY_D2D
    "Build the transitional Direct2D/DirectWrite backends (DEPRECATED -- to be deleted once Qt covers Windows)"
    ON)

if(TINTA_LEGACY_D2D)
    set(LEGACY_D2D_SOURCES src/render_d2d.cpp src/text_d2d.cpp)
    if(WIN32)
        message(STATUS
            "TINTA_LEGACY_D2D=ON -- building the transitional Direct2D/DirectWrite backends. "
            "These are scheduled for deletion; the target stack is Qt 6 on all three platforms.")
    endif()
else()
    set(LEGACY_D2D_SOURCES)
    message(STATUS
        "TINTA_LEGACY_D2D=OFF -- the Direct2D/DirectWrite backends are excluded. On Windows the "
        "app will COMPILE and fail to LINK, and those link errors are the point: they inventory "
        "what still depends on the stack being replaced.")
endif()

# Source files
set(SOURCES
    src/main_d2d.cpp
    src/markdown.cpp
    src/themes.cpp
    src/settings.cpp
    # settings.cpp is the portable ini reader/writer. Its two Windows halves were
    # split off: settings_app.cpp (the four functions that touch App) and
    # settings_win32.cpp (the file-association feature). Both are listed here
    # unconditionally because SOURCES is only ever consumed inside if(WIN32) --
    # the app target and the input-test variants. A non-Windows build reaches
    # settings.cpp through the test targets below, never through this list.
    src/settings_app.cpp
    src/settings_win32.cpp
    src/keymap.cpp
    src/config_dir.cpp
    ${PLATFORM_PATHS_SOURCE}
    ${PLATFORM_KEYBOARD_SOURCE}
    ${PLATFORM_IMAGE_SOURCE}
    ${PLATFORM_FILE_DIALOG_SOURCE}
    # The clipboard seam. On Windows this resolves to src/clipboard_win32.cpp, which
    # holds the ONLY remaining raw clipboard calls in the tree outside the image half
    # in src/export_docx.cpp -- src/utils.cpp's copyToClipboard, src/input.cpp's
    # clipboardLine and src/editor.cpp's two file-local helpers are all gone.
    ${PLATFORM_CLIPBOARD_SOURCE}
    src/signals.cpp
    src/tableedit.cpp
    src/d2d_init.cpp
    # The Direct2D/DirectWrite implementations of the drawing and text seams.
    # These used to be compiled only by render_d2d_tests, because nothing in the
    # app referenced tinta::RenderTarget -- the app called Direct2D directly. Now
    # the drawing sites go through the seam, so leaving them out of this list
    # produces a clean COMPILE and 25 unresolved externals at link: the reason
    # the app target needs them is exactly the reason the rewire is real.
    #
    # Now behind TINTA_LEGACY_D2D (see the option above). They are listed through
    # a variable rather than removed from this list conditionally, so the "clean
    # compile, unresolved at link" shape above stays exactly what OFF produces --
    # that shape is the inventory the flag exists to print.
    ${LEGACY_D2D_SOURCES}
    src/syntax.cpp
    src/utils.cpp
    # The relaunch out of utils.cpp: Ctrl+N re-launching this executable with
    # `--cascade --new`. Its own file because it is the one piece of utils.cpp with no
    # portable answer yet, and utils.cpp compiles off Windows now that it has left.
    #
    # IN SOURCES RATHER THAN BESIDE ITS PARSER, and that was measured rather than
    # preferred. The flags are parsed only in src/main_d2d.cpp's WinMain, which looks
    # like the obvious home -- but INPUT_TEST_SOURCES below is ${SOURCES} minus
    # main_d2d.cpp, and two of the five targets built from it reach this symbol through
    # editor.cpp and input.cpp. Putting it next to its parser would break all five.
    # Here it is inherited by every one of them for this single line.
    src/quicknote_win32.cpp
    # The text helpers out of utils.cpp that never needed Windows. Split off so a
    # non-Windows compiler can build them and text_utils_tests can run them; the
    # Windows feature half (title bar, shell handoff, clipboard, path
    # canonicalisation) stayed in utils.cpp.
    src/text_utils.cpp
    # The App-taking, backend-free half: one function, measureText's seam overload. Same
    # split and same naming as src/i18n_app.cpp. It is its own file rather than part of
    # text_utils.cpp above because that file's whole property is needing no app.h -- see
    # src/text_utils_app.cpp, which records the linker reading that forced the move.
    src/text_utils_app.cpp
    src/search.cpp
    src/render.cpp
    # The document paint pass, lifted out of main_d2d.cpp's WM_PAINT handler so a Qt
    # window can run the same 332 lines. Listed for BOTH platforms and both app
    # targets, which is the property that makes the extraction checkable: if Windows
    # and Qt ever draw a document differently, they are not sharing this file.
    src/paint_document.cpp
    # The two scrollbars, cut out of main_d2d.cpp's paint handler for the same reason and
    # on the same terms: BOTH platforms, both app targets, so a divergence in the most
    # visible piece of chrome in the product would have to be a divergence in this file.
    src/scrollbar.cpp
    src/file_utils.cpp
    src/overlays.cpp
    src/context_menu.cpp
    src/input.cpp
    src/editor.cpp
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
    src/document.cpp
    src/inline_style.cpp
    src/print.cpp
    src/i18n.cpp
    src/i18n_app.cpp
    src/selection.cpp
    src/math_render.cpp
    src/math_parser.cpp
    src/math_macros.cpp
    src/tabs.cpp
    src/export_html.cpp
    src/export_docx.cpp
    src/annotations.cpp
    src/drafts.cpp
    src/startpage.cpp
    src/editrail.cpp
    src/pandoc.cpp
)

set(HEADERS
    include/markdown.h
    include/types.h
    include/geometry.h
    include/app.h
    include/settings.h
    include/settings_data.h
    include/theme_data.h
    include/platform_paths.h
    include/config_dir.h
    include/keymap.h
    include/d2d_init.h
    include/syntax.h
    include/utils.h
    include/text_utils.h
    include/search.h
    include/render.h
    include/file_utils.h
    include/overlays.h
    include/input.h
    include/editor.h
    include/mermaid.h
    include/mermaid_ext.h
    include/document.h
    include/inline_style.h
    include/print.h
    include/i18n.h
    include/selection.h
    include/math_render.h
)

# The app itself is Windows-only (Direct2D/DirectWrite/Win32). Guarding it lets
# the platform-independent test targets below configure and run on Linux/macOS.
# That is the mechanism for keeping the portable core honest, and it IS enforced:
# scripts/probe-portable-linux.sh builds this file with g++ and asserts, in both
# directions, that exactly the files in tests/portable-sources.txt were compiled.
# (An earlier version of this comment said nothing enforced it, which stayed here
# after the probe was written and was itself read as evidence that the portable
# targets were idle. A comment claiming an absence needs re-checking as much as
# one claiming a presence.)
if(WIN32)

# Windows resource file (icon)
set(RESOURCES
    resources/tinta.rc
)

# Main executable
add_executable(tinta WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(tinta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(tinta PRIVATE
    md4c
    d2d1
    dwrite
    shell32
    windowscodecs
    urlmon
    ole32
    imm32
    d3d11
    dwmapi
    comdlg32
    shlwapi
    winspool
)

target_compile_definitions(tinta PRIVATE UNICODE _UNICODE
    ${TINTA_VERSION_DEFINITIONS})

# TINTA_D2D_BACKEND: "this target links the Direct2D/DirectWrite backend", which is a
# DIFFERENT QUESTION from _WIN32 and the difference is now load bearing.
#
# FOUND BY A LINK FAILURE, not by design. src/paint_document.cpp guarded its two native
# spots with #ifdef _WIN32, which was correct for every target that existed when it was
# written, because Windows and Direct2D were the same thing here. qinta broke that: it
# is a WINDOWS target with NO D2D backend, so _WIN32 was defined, the colour-emoji branch
# compiled, and the link came back
#
#   paint_document.obj : error LNK2019: unresolved external symbol nativeTextLayout
#
# out of src/text_d2d.cpp, which qinta correctly does not compile. The dashed-stroke
# block was worse and would NOT have shown up at link at all -- it resolves through
# d2d1.lib and app.d2dFactory, so under qinta it would have dereferenced a null
# factory at the first dashed mermaid connector.
#
# This is the shape of the whole port's endgame, arriving early: every `#ifdef _WIN32` in
# a file that BOTH stacks compile is asking about the OS when it means to ask about the
# backend. When src/render_d2d.cpp and src/text_d2d.cpp are deleted, this definition and
# both of its branches go with them.
#
# TWO FILES ARE CONVERTED NOW, not one. This comment used to claim paint_document.cpp was
# "the only such file with native spots inside it today", which was already false when it
# was written: include/app.h's three bitmap accessors -- titleIconBitmap,
# startPageIconBitmap, linkPeekBitmap -- had the same _WIN32 guard around a
# `seat<D2DBitmap>` call, and being in a HEADER they leaked the mistake into every
# translation unit that called them. Four objects with no Direct2D in them (tabs, overlays,
# startpage, editrail) each came back with `unresolved external symbol D2DBitmap::GetSize`.
# Corrected in place rather than deleted, because the wrong sentence is the useful part: a
# stale count in a comment reads as a considered decision about the files it excludes, which
# is strictly worse than reading as an omission. Nothing in this tree greps a prose count,
# so the next one will go stale the same way.
#
# Gated on TINTA_LEGACY_D2D so OFF turns the macro off too -- otherwise the flag would
# leave paint_document.cpp still compiling calls into backends the flag just removed, and
# the flag's whole job is to make that inventory honest.
#
# The INPUT_TEST_SOURCES targets inherit this through
# $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>, which is why it is set here rather than
# repeated on each of them.
if(TINTA_LEGACY_D2D)
    target_compile_definitions(tinta PRIVATE TINTA_D2D_BACKEND)
endif()

endif()  # WIN32 — app target

# --- THE QT APPLICATION: the target the whole port is aimed at ------------------
#
# One stack on every platform means this target eventually REPLACES `tinta` above
# rather than joining it, which is why src/render_d2d.cpp, src/text_d2d.cpp,
# src/settings_win32.cpp and src/platform_paths_win32.cpp are described as scheduled
# for deletion and not for porting. It is built on WINDOWS TOO wherever Qt is found:
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
    # The scroll policy, and the file dialog Ctrl+O opens. src/scroll.cpp is portable and
    # links off Windows; it holds the numbers src/input.cpp holds, in the one place both
    # stacks can reach, so the Qt window carries the translation from a QWheelEvent and
    # nothing else.
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
)

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
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network)
    # OUTSIDE the if(WIN32) below, deliberately: the version numbers are not a platform
    # fact, and every UNICODE definition in this file is guarded, so the tempting place to
    # add these is the one place they would silently not apply off Windows.
    target_compile_definitions(qinta PRIVATE ${TINTA_VERSION_DEFINITIONS})
    if(WIN32)
        # UNICODE/_UNICODE because the whole tree assumes the wide Win32 API, and
        # shell32 because ${PLATFORM_PATHS_SOURCE} is still the win32 file here. Both
        # go away at the cutover this target exists to reach: the path seam collapses
        # onto src/platform_paths_qt.cpp, which is written to be correct on all three.
        target_compile_definitions(qinta PRIVATE UNICODE _UNICODE)
        target_link_libraries(qinta PRIVATE shell32)
        # Dynamic CRT, the same override and the same measured reason as the Qt test
        # targets far below: Qt cannot be linked against the static CRT, and mixing the
        # two debug heaps in one process trips an assertion in Qt's own allocations.
        #
        # THIS IS WHERE THAT STOPS BEING A TEST-ONLY OVERRIDE. The comment at the top of
        # this file promises tinta.exe runs on a clean Windows install with no VC++
        # redistributable, and this target cannot keep that promise. It does not break
        # it either -- `tinta` above is untouched and still static -- but the two cannot
        # both survive the cutover, and which one gives way is Alex's call, not this
        # file's. README.md carries the three options.
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
    add_test(NAME platform_paths COMMAND platform_paths_tests)

    if(NOT WIN32)
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
    # (settings_app.cpp) is staying out on purpose and says why in its own header.
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
    # all in src/editor.cpp, which does not compile off Windows at all (fatal <imm.h>).
    # Its two pure functions, tsvLooksLikeTable and tsvToMarkdownTable, need no App and
    # no HWND and are assertable the moment that is settled.
    add_library(portable_compile_only OBJECT
        # src/annotations.cpp -- COMPILE-ONLY, and the reason is a link closure rather
        # than a compile problem. It reached 0 errors off Windows once the clipboard seam
        # landed and its wide-path ofstream became a named fs::path; those were the two
        # families its census row hid behind one label. What it still cannot do is LINK
        # into qinta: it calls handleFileWatchTimer, which lives in src/input.cpp, and
        # input.cpp is windows.h-fatal. So this is the honest home for it -- a
        # non-Windows compiler reads the file every build, which is what
        # tests/portable-sources.txt claims and all it claims.
        #
        # NOT added to QINTA_APP_SOURCES, deliberately: doing that would trade a green
        # build for a manifest entry, and the manifest is not a wish list.
        src/annotations.cpp
        src/editrail.cpp
        src/tableedit.cpp
        # src/text_utils_app.cpp -- one function, measureText's seam overload, split out
        # of the windows.h-fatal src/utils.cpp because a linker reading of qinta's
        # closure found it was the ONLY symbol render.cpp needed from that file.
        #
        # COMPILE-ONLY, and that is a stated gap rather than a quiet one. NOTHING in this
        # tree executes measureText on any platform -- grepping tests/ for the name
        # returns nothing, which also corrected a claim in utils.cpp that
        # text_factory_tests asserted the two overloads agree. It asserts the layer
        # beneath instead. Executing this overload needs an App with a live textFactory,
        # which the Qt seam tests deliberately do not build (they use a harness, not an
        # App), so an assertion here is its own slice and not a line in this one.
        src/text_utils_app.cpp
        # src/render.cpp ARRIVES HERE, and it is the largest single file this port has
        # moved -- but read what that does and does not mean, because the gap between
        # the three readings is unusually wide for this one file.
        #
        #   COMPILES off Windows: measured, exit 0 / fatal 0 / errors 0, under a census
        #   whose six controls all held. That is what this target asserts.
        #
        #   LINKS into a Qt app: YES, AS OF THIS COMMIT, and the sentence here used to
        #   say "NOT YET" with the residue named -- six symbols owned by
        #   src/math_render.cpp, which still does not compile off Windows. What changed
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
        # The scroll policy, here for the same reason and with a sharper edge on it. The
        # file exists so that src/input.cpp can call it once input.cpp ports, and
        # input.cpp will never link Qt -- so a QWheelEvent or a Qt::KeyboardModifier
        # drifting into scroll.cpp would quietly make the shared policy unshareable.
        # THIS TARGET IS THE ONLY THING THAT WOULD CATCH THAT: qt_entry_tests links Qt,
        # so it cannot tell the difference.
        src/scroll.cpp
        # The scrollbars, and this one has a Windows-flavoured edge rather than a Qt one.
        # The 95 lines came out of src/main_d2d.cpp, the least portable file in the tree,
        # and they drew through the seam already -- so the risk is not a Qt include
        # drifting in, it is a Direct2D one that was always there and that nothing had
        # ever asked about. This target is what asks.
        src/scrollbar.cpp
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
        #   commit. Its callees include tr(), dpi() and the theme helpers, several of
        #   which still live in windows.h-fatal files, so the closure is a separate
        #   reading with its own fixpoint -- exactly the render.cpp lesson, which took
        #   three linker readings to settle.
        #
        #   RUNS: NO. Nothing executes an overlay off Windows. The overlays are
        #   keyboard- and mouse-driven chrome and input.cpp has not ported.
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
        #   1. measureText(IDWriteTextFormat*) -> src/d2d_init.cpp, beside the four calls
        #      in updateTextFormats that were its only callers in the tree. It is why this
        #      file needed dwrite.h, and its portable twin already lived in
        #      src/text_utils_app.cpp -- so the file was unbuildable off Windows for one
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
        # files not on the manifest -- draftsRecoverAll and draftsDiscardAll (src/drafts.cpp,
        # <shlobj.h>-fatal) and saveEditorFile (src/editor.cpp, which carries no Windows
        # include at all, so nothing has ever read it end to end either). That is the next
        # question this list poses, and it is a question, not a plan.
        src/signals.cpp
    )
    target_include_directories(portable_compile_only PRIVATE include ${md4c_SOURCE_DIR}/src)
    # Unconditional, same argument as qinta above: a version number is not a platform fact.
    target_compile_definitions(portable_compile_only PRIVATE ${TINTA_VERSION_DEFINITIONS})
    if(WIN32)
        target_compile_definitions(portable_compile_only PRIVATE UNICODE _UNICODE)
    endif()

    if(WIN32)
    # Exercise the real input handlers and editor with a message-only test
    # window, including layout-dependent character sequences (#195).
    set(INPUT_TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM INPUT_TEST_SOURCES src/main_d2d.cpp)
    add_executable(input_tests tests/input_tests.cpp ${INPUT_TEST_SOURCES})
    target_include_directories(input_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(input_tests PRIVATE $<TARGET_PROPERTY:tinta,LINK_LIBRARIES>)
    target_compile_definitions(input_tests PRIVATE $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>
        TINTA_SHORTCUT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shortcuts-layouts.md")
    add_test(NAME input_shortcuts COMMAND input_tests)

    add_executable(file_path_tests tests/file_path_tests.cpp ${INPUT_TEST_SOURCES})
    target_include_directories(file_path_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(file_path_tests PRIVATE $<TARGET_PROPERTY:tinta,LINK_LIBRARIES>)
    target_compile_definitions(file_path_tests PRIVATE $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>)
    add_test(NAME copy_file_path COMMAND file_path_tests)

    add_executable(code_block_layout_tests tests/code_block_layout_tests.cpp ${INPUT_TEST_SOURCES})
    target_include_directories(code_block_layout_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(code_block_layout_tests PRIVATE $<TARGET_PROPERTY:tinta,LINK_LIBRARIES>)
    target_compile_definitions(code_block_layout_tests PRIVATE $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>
        TINTA_CODE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/code-block-spacing.md")
    add_test(NAME code_block_layout COMMAND code_block_layout_tests)

    add_executable(blockquote_layout_tests tests/blockquote_layout_tests.cpp ${INPUT_TEST_SOURCES})
    target_include_directories(blockquote_layout_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(blockquote_layout_tests PRIVATE $<TARGET_PROPERTY:tinta,LINK_LIBRARIES>)
    target_compile_definitions(blockquote_layout_tests PRIVATE $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>
        TINTA_QUOTE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blockquote-spacing.md")
    add_test(NAME blockquote_layout COMMAND blockquote_layout_tests)

    add_executable(theme_tests tests/theme_tests.cpp ${INPUT_TEST_SOURCES})
    target_include_directories(theme_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(theme_tests PRIVATE $<TARGET_PROPERTY:tinta,LINK_LIBRARIES>)
    target_compile_definitions(theme_tests PRIVATE $<TARGET_PROPERTY:tinta,COMPILE_DEFINITIONS>
        TINTA_THEME_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/inline-code-color.md")
    add_test(NAME inline_code_theme COMMAND ${CMAKE_COMMAND}
        -DTEST_BINARY=$<TARGET_FILE:theme_tests>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/theme-test-$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_test.cmake)

    # Needs no display and no render target: D2DBitmap borrows, so the assertions
    # use fake addresses they never dereference.
    #
    # IT STAYS HERE, and not for the reason it used to give. The old comment said it
    # linked the two backend sources "for the same reason context_menu_tests does --
    # App holds wrappers BY VALUE, so any target that instantiates App needs their
    # vtables", and that reason expired when App's storage moved to pointers to the
    # abstract seam. context_menu_tests has moved out above on the strength of that.
    # This one is Windows-only by SUBJECT: it asserts on D2DBitmap, the Direct2D
    # wrapper itself, and reseat-on-read is a property of that wrapper. Its Qt
    # counterpart would be a different set of assertions, not this set run elsewhere.
    # The next two are gated on TINTA_LEGACY_D2D because their SUBJECT is the
    # Direct2D backend -- D2DBitmap's reseat-on-read, and the DirectWrite factory
    # wrapper. When the backends are deleted these two are deleted with them; their
    # Qt counterparts are a different set of assertions, not this set run elsewhere.
    #
    # The distinction matters and is deliberate. Targets that link the backends only
    # INCIDENTALLY -- math_layout_tests, and everything built from INPUT_TEST_SOURCES
    # -- are NOT gated. Their subjects survive the stack change, so under
    # TINTA_LEGACY_D2D=OFF they are supposed to fail to link, and those failures are
    # the inventory the flag exists to print. Gating them would hide it.
    if(TINTA_LEGACY_D2D)
    add_executable(bitmap_seam_tests tests/bitmap_seam_tests.cpp
        src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        src/render_d2d.cpp src/text_d2d.cpp)
    target_include_directories(bitmap_seam_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(bitmap_seam_tests PRIVATE md4c shell32 d2d1 dwrite)
    # TINTA_D2D_BACKEND because this target COMPILES src/render_d2d.cpp two lines up, so the
    # macro's own definition -- "this target links the Direct2D/DirectWrite backend" -- is
    # simply true of it. It was missing, and that only became visible when include/app.h's
    # three bitmap accessors moved off #ifdef _WIN32 onto this macro: the accessors then took
    # the null branch here and the test reported exactly 10 failures, predicted in advance and
    # itemised -- the three "holds the native it was given", the three "follows a replaced
    # native", the three "keeps its own native", and the distinct-ADDRESS check, which fails
    # because three nullptrs compare equal. Worth recording that the four assertions in
    # nullNativeYieldsNullSeam kept PASSING throughout, trivially, everything being null: they
    # read like coverage of this seam and would not have noticed its removal.
    target_compile_definitions(bitmap_seam_tests PRIVATE UNICODE _UNICODE TINTA_D2D_BACKEND)
    add_test(NAME bitmap_seam COMMAND bitmap_seam_tests)

    # Unlike bitmap_seam_tests, this one uses REAL DirectWrite objects -- it can,
    # because DWriteCreateFactory is device-independent and needs no HWND or display.
    # Same link set and the same reason: App holds the seam wrappers BY VALUE.
    add_executable(text_factory_tests tests/text_factory_tests.cpp
        src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        src/render_d2d.cpp src/text_d2d.cpp)
    target_include_directories(text_factory_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(text_factory_tests PRIVATE md4c shell32 d2d1 dwrite)
    target_compile_definitions(text_factory_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME text_factory COMMAND text_factory_tests)
    endif()  # TINTA_LEGACY_D2D
    endif()  # WIN32

    add_executable(math_parser_tests tests/math_parser_tests.cpp src/math_parser.cpp src/math_macros.cpp)
    target_include_directories(math_parser_tests PRIVATE src)
    add_test(NAME math_parser COMMAND math_parser_tests)

    if(WIN32)
    # Through ${LEGACY_D2D_SOURCES}, not the literal file names, and that was a
    # correction: naming them literally left TINTA_LEGACY_D2D=OFF still COMPILING
    # both backends through this one target, so the option did not mean what its own
    # help string said. Found by grepping the OFF build log for the compile lines
    # rather than by trusting the failing-link list, which looked right.
    #
    # It is NOT gated on the flag, unlike bitmap_seam_tests and render_d2d_tests: the
    # subject here is math layout, which survives the stack change. So under OFF this
    # target is SUPPOSED to fail at link, and that failure is inventory rather than
    # noise.
    add_executable(math_layout_tests tests/math_layout_tests.cpp
        src/math_parser.cpp src/math_macros.cpp src/math_render.cpp src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        ${LEGACY_D2D_SOURCES})
    target_include_directories(math_layout_tests PRIVATE include src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(math_layout_tests PRIVATE md4c d2d1 dwrite windowscodecs shell32 ole32)
    target_compile_definitions(math_layout_tests PRIVATE UNICODE _UNICODE
        TINTA_MATH_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/math-compatibility.md")
    add_test(NAME math_layout COMMAND math_layout_tests)
    endif()  # WIN32

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
    if(WIN32)
        target_compile_definitions(clipboard_tests PRIVATE UNICODE _UNICODE)
    else()
        # The test constructs a QGuiApplication under this define, because QClipboard is
        # null without one and the round trip would otherwise report "refused" on every
        # run -- a skip that looks like a platform limitation and is actually a missing
        # object. The transformation checks do NOT need it and run either way.
        target_compile_definitions(clipboard_tests PRIVATE TINTA_CLIPBOARD_BACKEND_QT)
    endif()
    add_test(NAME clipboard COMMAND clipboard_tests)
    add_test(NAME clipboard_control COMMAND clipboard_tests --control)
    if(NOT WIN32)
        set_tests_properties(clipboard clipboard_control PROPERTIES
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
    # It sits here, after find_package, purely because the non-Windows half links Qt.
    # It is not part of the Qt6_FOUND block below, because the Windows half needs no
    # Qt at all and must still be tested: a seam with two implementations where only
    # one is ever asserted is the shape include/render_api.h was explicitly criticised
    # for ("an interface with one implementation -- which is indirection, not
    # abstraction").
    #
    # ${PLATFORM_KEYBOARD_SOURCE} resolves to keyboard_win32.cpp or keyboard_qt.cpp,
    # so this single target runs the same assertions against whichever backend the
    # platform selected -- and the assertions that carry the weight are on the pure
    # predicate in the header, which is identical either way. See the test's own
    # opening comment for what it can and cannot prove; the honest answer is mixed.
    #
    # No user32 line on Windows: GetKeyState is in user32, which CMake's default MSVC
    # libraries already include. Measured by building, not assumed.
    if(WIN32 OR Qt6_FOUND)
        add_executable(keyboard_tests tests/keyboard_tests.cpp ${PLATFORM_KEYBOARD_SOURCE})
        target_include_directories(keyboard_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        if(NOT WIN32)
            target_link_libraries(keyboard_tests PRIVATE Qt6::Core Qt6::Gui)
        endif()
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
    if(WIN32 OR (Qt6_FOUND AND Qt6Svg_FOUND AND Qt6Network_FOUND))
        add_executable(image_tests tests/image_tests.cpp ${PLATFORM_IMAGE_SOURCE})
        target_include_directories(image_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        if(WIN32)
            target_compile_definitions(image_tests PRIVATE UNICODE _UNICODE)
            # text_utils.cpp and the path seam are a WINDOWS-ONLY dependency here, and
            # listing them per-branch rather than in the target's own source list is
            # the honest spelling: image_win32.cpp calls toWide to widen a UTF-8 URL,
            # which reaches platform::fromUtf8, while image_qt.cpp converts through
            # QString and needs neither. shell32 is what platform_paths_win32.cpp
            # wants, the same line text_utils_tests carries.
            target_sources(image_tests PRIVATE
                src/text_utils.cpp ${PLATFORM_PATHS_SOURCE} ${LEGACY_D2D_SOURCES})
            # windowscodecs for WIC, d2d1 for the SVG device context, shlwapi for
            # SHCreateMemStream, urlmon for the fetch, ole32 for CoInitializeEx.
            # These are the libs render.cpp's image code needed; they moved with it.
            # dwrite arrives with ${LEGACY_D2D_SOURCES} -- text_d2d.cpp calls
            # DWriteCreateFactory, the same one line render_d2d_tests carries.
            target_link_libraries(image_tests PRIVATE
                windowscodecs d2d1 dwrite shlwapi urlmon ole32 shell32)
        else()
            target_sources(image_tests PRIVATE
                src/render_qt.cpp src/text_qt.cpp)
            target_link_libraries(image_tests PRIVATE
                Qt6::Core Qt6::Gui Qt6::Svg Qt6::Network)
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

        # THE QT TARGETS MUST USE THE DYNAMIC CRT, and the tree-wide setting at the
        # top of this file uses the static one on purpose. Both halves are deliberate,
        # so this is an override with a reason and not a contradiction.
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
        # WHAT THIS DOES NOT DECIDE. Qt cannot be linked against the static CRT, so
        # once render_qt.cpp and text_qt.cpp are the real backend rather than a tested
        # seam, tinta.exe itself faces the same choice and the "no VC++ redistributable"
        # promise in the comment at the top of this file is what pays for it. That is a
        # product decision, it is Alex's, and overriding two TEST targets deliberately
        # does not pre-empt it.
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
            add_executable(qt_entry_tests
                tests/qt_entry_tests.cpp
                ${QINTA_APP_SOURCES}
            )
            target_include_directories(qt_entry_tests PRIVATE
                include ${md4c_SOURCE_DIR}/src)
            # Svg and Network arrive with the closure, not with the test: image_qt.cpp's
            # QSvgRenderer and QNetworkAccessManager carry fourteen of the twenty
            # residual symbols the fixpoint left. Same two lines as qinta, for the
            # same measured reason.
            target_link_libraries(qt_entry_tests PRIVATE
                md4c Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network)
            if(WIN32)
                target_compile_definitions(qt_entry_tests PRIVATE UNICODE _UNICODE)
                target_link_libraries(qt_entry_tests PRIVATE shell32)
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
        else()
            message(STATUS "Qt6 Widgets not found -- qt_entry test skipped")
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
        if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            # Built as a list because qt_entry only exists where Widgets was found, and
            # naming a test that does not exist is a configure error -- which would
            # delete the whole Qt block on a partial install rather than one target.
            set(TINTA_QT_PATH_TESTS qt_text render_api)
            if(Qt6Widgets_FOUND)
                list(APPEND TINTA_QT_PATH_TESTS qt_entry)
            endif()
            set_tests_properties(${TINTA_QT_PATH_TESTS} PROPERTIES
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<SHELL_PATH:$<TARGET_FILE_DIR:Qt6::Core>>")
        endif()

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

    # The Direct2D side of the drawing seam, running the SAME assertions as
    # render_api_tests via tests/render_api_shared.cpp. This is what turns
    # include/render_api.h from an interface with one implementation -- which is
    # indirection, not abstraction -- into a measured two-backend contract: the seam
    # was shaped by the Qt backend, and this checks it back against the API it was
    # modelled on. It renders to a WIC bitmap (software target, no display, no
    # message loop), the same recipe tests/math_layout_tests.cpp already uses.
    #
    # NOT in tests/portable-sources.txt, deliberately: src/render_d2d.cpp is
    # Windows-only by design, so it must never appear in a non-Windows build's object
    # set. The manifest lists what the PORTABLE build compiles.
    # Gated on TINTA_LEGACY_D2D for the same reason as bitmap_seam_tests: the
    # subject IS the backend. render_api_tests runs these same assertions against
    # the Qt backend, so what this target proves survives its own deletion.
    if(TINTA_LEGACY_D2D)
    add_executable(render_d2d_tests
        tests/render_d2d_tests.cpp
        tests/render_api_shared.cpp
        src/render_d2d.cpp
        src/text_d2d.cpp
    )
    target_include_directories(render_d2d_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    # dwrite for DWriteCreateFactory, which the harness calls to build the factory
    # src/text_d2d.cpp then borrows. Everything else text_d2d.cpp does is a COM
    # method call through that pointer and needs no import library at all -- which
    # is why this line is the whole cost of adding the text backend here.
    target_link_libraries(render_d2d_tests PRIVATE d2d1 dwrite windowscodecs ole32)
    target_compile_definitions(render_d2d_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME render_d2d COMMAND render_d2d_tests)
    endif()  # TINTA_LEGACY_D2D
    endif()  # WIN32
endif()

# Install
if(WIN32)
    install(TARGETS tinta RUNTIME DESTINATION bin)
endif()

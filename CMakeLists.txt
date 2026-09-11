cmake_minimum_required(VERSION 3.15)
project(tinta VERSION 3.6.7 LANGUAGES C CXX)

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

# The platform path seam has one implementation per family. Selected here rather
# than with #ifdefs inside one file so that a Windows-only API can never be
# reached from a POSIX build even by accident, and so adding a platform means
# adding a file rather than editing a shared one.
if(WIN32)
    set(PLATFORM_PATHS_SOURCE src/platform_paths_win32.cpp)
else()
    set(PLATFORM_PATHS_SOURCE src/platform_paths_posix.cpp)
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
    src/signals.cpp
    src/tableedit.cpp
    src/d2d_init.cpp
    # The Direct2D/DirectWrite implementations of the drawing and text seams.
    # These used to be compiled only by render_d2d_tests, because nothing in the
    # app referenced tinta::RenderTarget -- the app called Direct2D directly. Now
    # the drawing sites go through the seam, so leaving them out of this list
    # produces a clean COMPILE and 25 unresolved externals at link: the reason
    # the app target needs them is exactly the reason the rewire is real.
    src/render_d2d.cpp
    src/text_d2d.cpp
    src/syntax.cpp
    src/utils.cpp
    src/search.cpp
    src/render.cpp
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
    TINTA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TINTA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TINTA_VERSION_PATCH=${PROJECT_VERSION_PATCH})

endif()  # WIN32 — app target

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
    if(WIN32)
        # SHGetFolderPathW lives in shell32; the POSIX build needs no extra libs.
        target_link_libraries(platform_paths_tests PRIVATE shell32)
        target_compile_definitions(platform_paths_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME platform_paths COMMAND platform_paths_tests)

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

    add_executable(context_menu_tests tests/context_menu_tests.cpp src/context_menu.cpp
        src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        src/render_d2d.cpp src/text_d2d.cpp)
    target_include_directories(context_menu_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(context_menu_tests PRIVATE md4c shell32 d2d1 dwrite)
    target_compile_definitions(context_menu_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME context_menu COMMAND context_menu_tests)

    # Needs no display and no render target: D2DBitmap borrows, so the assertions
    # use fake addresses they never dereference. It links the two backend sources
    # for the same reason context_menu_tests does -- App holds wrappers BY VALUE,
    # so any target that instantiates App needs their vtables.
    add_executable(bitmap_seam_tests tests/bitmap_seam_tests.cpp
        src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        src/render_d2d.cpp src/text_d2d.cpp)
    target_include_directories(bitmap_seam_tests PRIVATE include ${md4c_SOURCE_DIR}/src)
    target_link_libraries(bitmap_seam_tests PRIVATE md4c shell32 d2d1 dwrite)
    target_compile_definitions(bitmap_seam_tests PRIVATE UNICODE _UNICODE)
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
    endif()  # WIN32

    add_executable(math_parser_tests tests/math_parser_tests.cpp src/math_parser.cpp src/math_macros.cpp)
    target_include_directories(math_parser_tests PRIVATE src)
    add_test(NAME math_parser COMMAND math_parser_tests)

    if(WIN32)
    add_executable(math_layout_tests tests/math_layout_tests.cpp
        src/math_parser.cpp src/math_macros.cpp src/math_render.cpp src/markdown.cpp src/themes.cpp src/config_dir.cpp ${PLATFORM_PATHS_SOURCE}
        src/render_d2d.cpp src/text_d2d.cpp)
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
    add_executable(i18n_tests
        tests/i18n_tests.cpp
        src/i18n.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(i18n_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(WIN32)
        # shell32 for ShellExecuteW/SHGetFolderPathW inside the win32 seam.
        target_link_libraries(i18n_tests PRIVATE shell32)
        target_compile_definitions(i18n_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME i18n COMMAND i18n_tests)

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
    if(WIN32)
        target_link_libraries(settings_tests PRIVATE shell32)
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
    if(WIN32)
        target_link_libraries(theme_data_tests PRIVATE shell32)
        target_compile_definitions(theme_data_tests PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME theme_data COMMAND ${CMAKE_COMMAND}
        -DTEST_BINARY=$<TARGET_FILE:theme_data_tests>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/theme-data-test-$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_portable_test.cmake)

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
    find_package(Qt6 QUIET COMPONENTS Core Gui)
    if(Qt6_FOUND)
        add_executable(qt_text_tests tests/qt_text_tests.cpp)
        target_link_libraries(qt_text_tests PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME qt_text COMMAND qt_text_tests)
        set_tests_properties(qt_text PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
        # This one paints into a QImage and draws no text, so it does not depend on
        # a font database the way qt_text does. offscreen is set anyway: the arm
        # that matters is that no arm reaches for a real display, and pinning it
        # keeps the two Qt targets from disagreeing about their environment.
        set_tests_properties(render_api PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
    if(WIN32)
    add_executable(inline_style_tests
        tests/inline_style_tests.cpp
        src/inline_style.cpp
        src/render_d2d.cpp
        src/text_d2d.cpp
        src/markdown.cpp
        src/themes.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(inline_style_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${md4c_SOURCE_DIR}/src
    )
    target_link_libraries(inline_style_tests PRIVATE md4c d2d1 dwrite windowscodecs shell32)
    target_compile_definitions(inline_style_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME inline_styles COMMAND inline_style_tests)

    add_executable(config_path_tests
        tests/config_path_tests.cpp
        src/config_dir.cpp
        ${PLATFORM_PATHS_SOURCE}
    )
    target_include_directories(config_path_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(config_path_tests PRIVATE shell32)
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
    endif()  # WIN32
endif()

# Install
if(WIN32)
    install(TARGETS tinta RUNTIME DESTINATION bin)
endif()

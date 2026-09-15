#!/usr/bin/env bash
#
# Builds and runs tinta's platform-independent targets with a non-Windows
# compiler, and reports enough detail to tell a real pass from a vacuous one.
#
# This script exists because the CI job that used to do this was removed on
# 2026-09-10. A green Windows build cannot stand in for it: MSVC compiles the
# portable targets happily whether or not they still depend on Windows, so it
# can prove header topology (via cl /showIncludes) but never compiler
# acceptance. Only a GCC/Clang build reads the code the way Linux and macOS
# will -- MSVC extensions, headers MSVC supplies transitively, and the width of
# wchar_t are all invisible until then.
#
# Run from anywhere:  scripts/probe-portable-linux.sh
# Under WSL:          wsl.exe -- bash /mnt/c/src/tinta/scripts/probe-portable-linux.sh
#
# Exit 0 only if every assertion below holds.

set -u -o pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Deliberately NOT inside the source tree: on WSL the tree lives on /mnt/c, where
# builds are slow and file modes are synthetic. It is also kept away from the
# Windows build/ directory so the two caches can never poison each other -- a
# CMakeCache.txt written by MSVC names a compiler that does not exist here.
BUILD_DIR="${TINTA_LINUX_BUILD_DIR:-${HOME}/.cache/tinta-linux-build}"

failures=0
note() { printf '\n== %s\n' "$1"; }
check() {
    if [ "$1" = "0" ]; then
        printf 'ok   : %s\n' "$2"
    else
        printf 'FAIL : %s\n' "$2"
        failures=$((failures + 1))
    fi
}

CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
    for candidate in g++ clang++; do
        if command -v "$candidate" >/dev/null 2>&1; then CXX_BIN="$candidate"; break; fi
    done
fi
CMAKE_BIN="${CMAKE:-cmake}"

note 'Toolchain'
if [ -z "$CXX_BIN" ] || ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    echo "FAIL : no C++ compiler found (looked for \$CXX, g++, clang++)."
    echo "       Install one, e.g.: sudo apt-get install -y g++ cmake make"
    exit 1
fi
if ! command -v "$CMAKE_BIN" >/dev/null 2>&1; then
    echo "FAIL : cmake not found. Install it, e.g.: sudo apt-get install -y cmake"
    exit 1
fi
echo "compiler : $(command -v "$CXX_BIN")"
"$CXX_BIN" --version | head -1
echo "cmake    : $("$CMAKE_BIN" --version | head -1)"
echo "source   : $SOURCE_DIR"
echo "build    : $BUILD_DIR"

# --- NEGATIVE CONTROL ------------------------------------------------------
# Everything below concludes "these files do not need Windows" from the fact
# that they compiled. That inference is only worth anything if this environment
# would actually REJECT a file that did need Windows. If some SDK copy or a
# MinGW cross-header were on the include path, windows.h would resolve here and
# every pass below would be meaningless. So prove the environment can say no.
note 'Negative control: windows.h must NOT be available'
control_src="$(mktemp --suffix=.cpp)"
printf '#include <windows.h>\nint main() { return 0; }\n' > "$control_src"
if "$CXX_BIN" -fsyntax-only "$control_src" >/dev/null 2>&1; then
    check 1 "CONTROL: windows.h is UNAVAILABLE (it compiled -- this environment is not a valid witness)"
else
    check 0 "CONTROL: windows.h is unavailable, so a Windows dependency here really does fail"
fi
rm -f "$control_src"

note 'Configure'
rm -rf "$BUILD_DIR"
"$CMAKE_BIN" -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_CXX_COMPILER="$CXX_BIN" > "$BUILD_DIR.configure.log" 2>&1
check "$?" "cmake configure succeeds off Windows"
tail -5 "$BUILD_DIR.configure.log"

note 'Build (verbose, so every object is named)'
"$CMAKE_BIN" --build "$BUILD_DIR" -- VERBOSE=1 > "$BUILD_DIR.build.log" 2>&1
build_status=$?
check "$build_status" "the platform-independent targets build with $CXX_BIN"
if [ "$build_status" != "0" ]; then
    echo '--- last 40 lines of build log ---'
    tail -40 "$BUILD_DIR.build.log"
fi

# Per-object evidence. "The build succeeded" is not the claim -- a target that
# was silently skipped also succeeds. Each source file must appear as its own
# compile line, which is the only thing that shows this compiler read it.
#
# This used to be a hand-written list of five names, and that was the wrong shape
# of instrument. It could only ever confirm what someone had thought to look for,
# so when the portable surface grew to 22 files the probe kept reporting on five
# -- and on 2026-09-10 that under-report was read as "the other 17 have never been
# compiled off Windows", which was false and sent a whole recommendation the wrong
# way. The list is now a committed manifest and the comparison runs BOTH ways, so
# growth is as loud as shrinkage. See tests/portable-sources.txt.
note 'Portable source set (both directions against tests/portable-sources.txt)'
manifest="$SOURCE_DIR/tests/portable-sources.txt"
if [ ! -f "$manifest" ]; then
    check 1 "tests/portable-sources.txt exists -- it is the expected half of this comparison"
else
    check 0 "tests/portable-sources.txt found"
    expected="$(grep -vE '^[[:space:]]*(#|$)' "$manifest" | tr -d '\r' | sort -u)"
    # Source objects only. A test's own translation unit is evidence about the
    # test, not about how much of the application is portable.
    #
    # The filter is the SOURCE DIRECTORY, not a filename convention. It used to
    # exclude names ending in _tests.cpp, which is a guess about naming rather than
    # a fact about location, and tests/render_api_shared.cpp broke it: a test file
    # that holds shared assertions rather than a main(), so it does not carry the
    # suffix. It was reported UNEXPECTED, and the obvious repair -- adding a line to
    # the manifest -- would have been wrong twice over. The manifest documents src/
    # files, and the portable-share count below prefixes every entry with src/, so
    # the entry would have named a file that does not exist and quietly corrupted
    # the figure this whole section was rewritten to get right.
    #
    # CMake writes object paths mirroring the source tree, so /src/ vs /tests/ is
    # readable straight off the log. If that convention ever changed this set would
    # come out EMPTY, and an empty set makes every manifest entry MISSING -- so the
    # failure mode is 24 loud lines, not a quiet pass.
    compiled="$(grep -oE '/src/[A-Za-z0-9_]+\.cpp\.o' "$BUILD_DIR.build.log" \
        | sed 's#^/src/##; s/\.o$//' | sort -u)"
    n_expected="$(printf '%s\n' "$expected" | grep -c . || true)"
    n_compiled="$(printf '%s\n' "$compiled" | grep -c . || true)"
    echo "manifest lists : $n_expected"
    echo "build compiled : $n_compiled"

    missing="$(comm -23 <(printf '%s\n' "$expected") <(printf '%s\n' "$compiled") | grep -v '^$' || true)"
    unexpected="$(comm -13 <(printf '%s\n' "$expected") <(printf '%s\n' "$compiled") | grep -v '^$' || true)"

    if [ -z "$missing" ]; then
        check 0 "every file in the manifest was compiled by $CXX_BIN"
    else
        check 1 "every file in the manifest was compiled by $CXX_BIN (missing below)"
        printf '%s\n' "$missing" | sed 's/^/       MISSING: /'
    fi
    if [ -z "$unexpected" ]; then
        check 0 "nothing was compiled that the manifest does not list"
    else
        check 1 "nothing was compiled that the manifest does not list (add these deliberately)"
        printf '%s\n' "$unexpected" | sed 's/^/       UNEXPECTED: /'
    fi

    # Print the share rather than leaving it to be guessed. The mistake this
    # section was rewritten for was a wrong FIGURE, not a wrong pass/fail, so the
    # figure is now produced by the probe every run and never from memory.
    portable_lines="$(printf '%s\n' "$expected" | sed "s#^#${SOURCE_DIR}/src/#" \
        | tr '\n' '\0' | xargs -0 wc -l 2>/dev/null | tail -1 | awk '{print $1}')"
    total_lines="$(find "$SOURCE_DIR/src" "$SOURCE_DIR/include" \
        \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 wc -l | tail -1 | awk '{print $1}')"
    if [ -n "${portable_lines:-}" ] && [ -n "${total_lines:-}" ] && [ "$total_lines" -gt 0 ]; then
        awk -v p="$portable_lines" -v t="$total_lines" \
            'BEGIN { printf "portable share : %d of %d lines in src+include (%.1f%%)\n", p, t, 100*p/t }'
        check 0 "the portable share was computed from the manifest, not quoted from memory"
    else
        check 1 "the portable share was computed (line counting failed)"
    fi
fi

# Nothing Windows-only may have been dragged in. Each of these is inside if(WIN32)
# or listed only in a Windows-only target, so none may appear at all.
#
# settings_app.cpp and settings_win32.cpp are the halves that were split OFF
# settings.cpp so that it could compile here. Asserting their absence is what
# makes the settings.cpp pass above mean something: if a future edit put App or a
# registry call back into the portable file, or wired these into a portable
# target, this is where it shows.
#
# The `settings` loop entry above matches settings.cpp.o only -- the object names
# differ -- but check that too, because a grep that accidentally matched
# settings_app.cpp.o would report a pass for the wrong file.
for forbidden in main_d2d settings_app settings_win32; do
    if grep -qE "(^|[/ ])${forbidden}\.cpp\.o" "$BUILD_DIR.build.log"; then
        check 1 "src/${forbidden}.cpp stayed out of this build (it was compiled)"
        grep -oE "[^ ]*${forbidden}\.cpp\.o" "$BUILD_DIR.build.log" | head -1 | sed 's/^/       /'
    else
        check 0 "src/${forbidden}.cpp stayed out of this build, as a Windows-only file"
    fi
done

# The settings round-trip has to be one of the tests that actually ran, not just
# one that configured. It is the only assertion here that exercises the ini reader
# and writer rather than merely compiling them.
if grep -qE "settings_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/settings_tests.cpp was compiled by $CXX_BIN"
else
    check 1 "tests/settings_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# include/geometry.h is header-only, so it appears in no object list and the
# manifest above cannot see it. Every site that uses it is still in a
# Windows-only file, which means that without this target the non-Windows build
# would compile none of the header at all -- and a header nobody compiles is a
# header nobody is checking. Assert the test object directly: if the target is
# ever dropped or moved inside if(WIN32), the ctest count would simply go down
# and the run below would still pass.
if grep -qE "geometry_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/geometry_tests.cpp was compiled by $CXX_BIN, so include/geometry.h was read off Windows"
else
    check 1 "tests/geometry_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# include/render_api.h is header-only for the same reason and needs the same
# treatment. src/render_qt.cpp IS in the manifest above, so the backend is
# watched; this asserts the TEST that reads the framebuffer back. Dropping it
# would leave a Qt backend that compiles and is never once checked against a
# pixel -- and "it compiles" is exactly the evidence that was not enough when
# i18n.cpp had a clean include closure and still failed to build.
if grep -qE "render_api_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/render_api_tests.cpp was compiled by $CXX_BIN, so include/render_api.h was read off Windows"
else
    check 1 "tests/render_api_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# render_api_tests.cpp is now only the Qt HARNESS -- every assertion lives in
# tests/render_api_shared.cpp, which the Direct2D test compiles too. So the object
# line above no longer proves any assertion ran off Windows: if the shared file
# were dropped from this target, render_api_tests would fail to link, but if it
# were ever made Windows-only the Linux run would go quiet rather than red. Assert
# it separately, because the file whose absence is silent is the one worth naming.
if grep -qE "render_api_shared\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/render_api_shared.cpp was compiled by $CXX_BIN, so the shared assertion body runs off Windows"
else
    check 1 "tests/render_api_shared.cpp was compiled by $CXX_BIN (no object line found)"
fi

# tests/app_fixture.h needs naming for the same reason as the two headers above,
# and it is the one that matters most for the port: it is the synthetic App layout
# that lets anything taking an App& be asserted off Windows at all. It appears in
# no object list, the src/ manifest cannot see it, and its only consumer is a
# single target -- so if selection_layout_tests were dropped or moved inside
# if(WIN32), the ctest count would fall by one and every check in this script
# would still pass. That is the failure shape this file exists to make loud.
if grep -qE "selection_layout_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/selection_layout_tests.cpp was compiled by $CXX_BIN, so tests/app_fixture.h was read off Windows"
else
    check 1 "tests/selection_layout_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# include/syntax.h has exactly ONE consumer, tests/syntax_tests.cpp, and syntax.cpp
# is in the manifest above -- so the SOURCE is watched and the header is not. Naming
# the test object is the difference between "the highlighter still compiles" and
# "the highlighter is still asserted": if syntax_tests were dropped or moved inside
# if(WIN32), src/syntax.cpp would keep appearing in the manifest comparison via
# nothing at all -- it has no other portable target now that it has left
# portable_compile_only -- and this script would be the only thing that noticed.
# That is the shape measured twice before on this tree: hiding a target drops the
# ctest count by one while ctest still prints "100% tests passed".
if grep -qE "syntax_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/syntax_tests.cpp was compiled by $CXX_BIN, so include/syntax.h is asserted off Windows"
else
    check 1 "tests/syntax_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# The typography value types in include/text_api.h have exactly ONE consumer,
# tests/typography_tests.cpp, and that target links NO src/ file at all -- so the
# manifest above cannot see it in either direction. It is the purest case of the
# failure shape this section exists for: drop the target and nothing goes red, the
# ctest count simply falls by one while ctest prints "100% tests passed".
#
# It is also the one where losing the assertion costs the most, because of WHAT it
# asserts. Qt 6.4.2 cannot honour `dlig`, so the port drops discretionary ligatures
# on code spans by decision. A dropped OpenType feature produces no error and no
# visible failure -- just text with one fewer substitution. This test is the only
# instrument in the repo that can tell the deliberate drop from an accidental one,
# and the only instrument that can tell THIS test is still running is this check.
if grep -qE "typography_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/typography_tests.cpp was compiled by $CXX_BIN, so the typography seam types and the dropped-dlig divergence are asserted off Windows"
else
    check 1 "tests/typography_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# tests/search_tests.cpp is a DIFFERENT case from the three above, and the
# difference is worth stating rather than copying the reasoning. src/search.cpp IS on
# the manifest, and search_tests is now its only portable consumer, so simply
# dropping the target would show up as a MISSING file in the comparison above. This
# check is not belt-and-braces for that.
#
# It is here for the revert shape, which the manifest CANNOT see: put src/search.cpp
# back into portable_compile_only and drop search_tests, and the manifest is green
# again -- the file still compiles, still appears in the build log, still matches its
# line above -- while every assertion in that file, and the only execution of it on
# any platform, are gone. That is one plausible edit, not a contrived one: it is
# exactly what undoing this slice looks like. The object line tells the two apart.
#
# This comment said "40 assertions" for one span and the test printed 53 -- measured
# by running the binary, not by reading the source. No count is named here now, on
# purpose: the test prints its own on success, so the only copy of the number is the
# one that cannot go stale. Same defect as the two counts corrected in CMakeLists.txt.
if grep -qE "search_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/search_tests.cpp was compiled by $CXX_BIN, so src/search.cpp is asserted off Windows and not merely compiled"
else
    check 1 "tests/search_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# tests/file_utils_tests.cpp is the search_tests case exactly, with one addition
# worth naming because it is the only guard here that protects a SEAM's caller.
#
# src/file_utils.cpp is on the manifest and this target is its only portable consumer,
# so dropping the target reads as MISSING above. What the manifest cannot see is the
# revert shape: move file_utils.cpp into portable_compile_only, drop the test, and the
# comparison goes green with nothing executing a line of it -- and unlike search.cpp,
# reverting THIS file is a live possibility, because its portability rests on one seam
# call that a future slice might want to reshape.
#
# The five functions in it had zero coverage on either platform before this test, so a
# silent revert would not lose coverage that Windows still has. It would lose all of it.
if grep -qE "file_utils_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/file_utils_tests.cpp was compiled by $CXX_BIN, so src/file_utils.cpp and platform::fileWriteTicks are asserted off Windows and not merely compiled"
else
    check 1 "tests/file_utils_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# src/i18n_app.cpp needs a DIFFERENT SHAPE of check from every guard above, and the
# reason is worth stating because it took a wrong first attempt to see it.
#
# Every check so far greps for an OBJECT line, which answers "did this compiler read
# this file". For i18n_app.cpp that question cannot distinguish the two states that
# matter: whether it is compiled as part of portable_compile_only (nothing executes
# it) or compiled INTO i18n_tests (82 assertions run through it). The object path
# CMake writes is /src/i18n_app.cpp.o either way, so an object-line guard here would
# pass in both worlds -- and so would the manifest comparison above, since the file
# is listed and would be compiled regardless.
#
# The LINK line is what tells them apart: it names every object going into the
# binary. So this asserts i18n_app.cpp.o appears on the command that produces
# i18n_tests. Move the file back to compile-only and this is the only thing in the
# repo that goes red -- the ctest count would not even change, because the i18n test
# would still exist and still pass, just without ever calling the App overload.
if grep -E -- '-o i18n_tests( |$)' "$BUILD_DIR.build.log" | grep -q "i18n_app\.cpp\.o"; then
    check 0 "src/i18n_app.cpp is LINKED INTO i18n_tests, so the App overload of tr() is executed off Windows and not merely compiled"
else
    check 1 "src/i18n_app.cpp is linked into i18n_tests (not found on the link line -- has it gone back to portable_compile_only?)"
fi

# include/keyboard.h -- the modifier-key seam -- has exactly ONE consumer,
# tests/keyboard_tests.cpp, so it needs an object-line guard for the same reason
# geometry.h, render_api.h, syntax.h and app_fixture.h do: a header appears in no
# object list, so the manifest comparison above is structurally blind to it.
#
# src/keyboard_qt.cpp IS in the manifest, so the Qt BACKEND is watched in both
# directions. What is not watched is the assertions, and here that gap is unusually
# wide: the backend is two forwarding calls, while the pure predicate in the header
# is the thing carrying every assertion with any weight -- 131,086 of them, an
# exhaustive sweep of modifierHeld and keyToggled over all 65,536 short values.
# Dropping the target would leave keyboard_qt.cpp still listed, still compiled, still
# matching the manifest, with nothing at all checking the bit test that all 17
# rewired call sites depend on.
if grep -qE "keyboard_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/keyboard_tests.cpp was compiled by $CXX_BIN, so include/keyboard.h is asserted off Windows"
else
    check 1 "tests/keyboard_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# And the BACKEND has to be on the link line, which is a different question from the
# object line above and from the manifest. src/keyboard_qt.cpp could be compiled by
# portable_compile_only, satisfy the manifest, and never reach keyboard_tests -- in
# which case the target would fail to link, loudly. The reason to assert it anyway is
# the reverse edit: keyboard_tests linking src/keyboard_win32.cpp off Windows would be
# a configuration error that the manifest reads as UNEXPECTED but which no check here
# would attribute to the seam. Naming the file that must be there says which backend
# the 2 platform assertions actually exercised, which is the only thing they prove.
if grep -E -- '-o keyboard_tests( |$)' "$BUILD_DIR.build.log" | grep -q "keyboard_qt\.cpp\.o"; then
    check 0 "src/keyboard_qt.cpp is LINKED INTO keyboard_tests, so the Qt backend is the one asserted off Windows"
else
    check 1 "src/keyboard_qt.cpp is linked into keyboard_tests (not on the link line -- did the platform selection pick the win32 file?)"
fi

# The path seam's backend, and the argument above transplanted exactly. There is a
# sharper reason to assert this one: the two backends DISAGREE about a directory's
# write time, and platform_paths_tests keys that assertion on TINTA_PATHS_BACKEND_QT
# rather than on the OS. So a selection that quietly picked a different file would
# not fail -- it would compile the OTHER arm of that #ifdef and report a green suite
# for the backend nobody shipped.
if grep -E -- '-o platform_paths_tests( |$)' "$BUILD_DIR.build.log" | grep -q "platform_paths_qt\.cpp\.o"; then
    check 0 "src/platform_paths_qt.cpp is LINKED INTO platform_paths_tests, so the Qt backend is the one the 4x assertions exercised"
else
    check 1 "src/platform_paths_qt.cpp is linked into platform_paths_tests (not on the link line -- did the platform selection pick the win32 file, or is platform_paths_posix.cpp back?)"
fi

# And the DELETED file must stay deleted. Without this, restoring
# src/platform_paths_posix.cpp and reselecting it would show up only as a manifest
# mismatch on a name nobody reads as a regression -- while every assertion in
# platform_paths_tests kept passing, because both backends satisfy almost all of them.
if [ -e "$SOURCE_DIR/src/platform_paths_posix.cpp" ]; then
    check 1 "src/platform_paths_posix.cpp stays deleted (it is back on disk -- the Qt file REPLACED it rather than joining it)"
else
    check 0 "src/platform_paths_posix.cpp stays deleted -- the path seam has one non-Windows implementation, and it is Qt"
fi

# tests/image_tests.cpp, for the same structural reason as every test object above:
# include/image_api.h is a header, so the manifest cannot see it, and image_tests is
# its ONLY consumer on either platform.
#
# This one has a second failure mode the others do not. Its CMake guard is
# `WIN32 OR (Qt6_FOUND AND Qt6Svg_FOUND AND Qt6Network_FOUND)`, so a Qt install
# missing Svg or Network deletes the target while leaving Core and Gui -- and every
# other Qt target -- perfectly green. src/image_qt.cpp being in the manifest catches
# that too, but only via a MISSING line that reads like "Qt is gone"; this check says
# which target actually vanished.
if grep -qE "image_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/image_tests.cpp was compiled by $CXX_BIN, so include/image_api.h is asserted off Windows"
else
    check 1 "tests/image_tests.cpp was compiled by $CXX_BIN (no object line found -- is Qt Svg or Qt Network missing?)"
fi

# And the Qt backend on the link line, the keyboard argument transplanted: the
# manifest reads src/image_qt.cpp as compiled whether portable_compile_only or
# image_tests produced the object, so naming it here is what says WHICH backend the
# 56 assertions measured. The reverse edit is the one worth guarding -- image_tests
# linking src/image_win32.cpp off Windows would fail at <urlmon.h> rather than
# silently, but the manifest would attribute it to nothing in particular.
if grep -E -- '-o image_tests( |$)' "$BUILD_DIR.build.log" | grep -q "image_qt\.cpp\.o"; then
    check 0 "src/image_qt.cpp is LINKED INTO image_tests, so the Qt backend is the one asserted off Windows"
else
    check 1 "src/image_qt.cpp is linked into image_tests (not on the link line -- did the platform selection pick the win32 file?)"
fi

# --- The Qt frontend -------------------------------------------------------------
#
# tests/qt_entry_tests.cpp is a test object, so the manifest is blind to it the same
# way it is blind to every header. It is worth naming for a reason none of the guards
# above have: it is the ONLY test on this tree that paints a tinta WINDOW. Every other
# drawing assertion paints into a bitmap, so if this target were dropped the suite
# would keep asserting the seam and stop asserting that App's storage, a real paint
# event and QWidget::grab() work together at all -- and the ctest count would simply
# fall by one while ctest printed "100% tests passed".
if grep -qE "qt_entry_tests\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 0 "tests/qt_entry_tests.cpp was compiled by $CXX_BIN, so include/qt_init.h and include/qt_window.h are read off Windows"
else
    check 1 "tests/qt_entry_tests.cpp was compiled by $CXX_BIN (no object line found)"
fi

# And the two frontend files on that test's LINK line, which is a different question
# from the manifest. src/qt_init.cpp and src/qt_window.cpp are both listed above, and
# src/tinta_qt compiles both -- so the manifest reads them as compiled whether or not
# any test links them. The revert shape the manifest cannot see is exactly that: leave
# them in the app target, drop them from the test, and the comparison stays green with
# nothing executing a line of either.
#
# What is lost in that world is specific and worth naming: Seated<>::install() goes
# back to having no caller under test, and the install/release symmetry, the QPainter's
# scope and the glyph pixels all stop being measured.
for frontend in qt_init qt_window; do
    if grep -E -- '-o qt_entry_tests( |$)' "$BUILD_DIR.build.log" | grep -q "${frontend}\.cpp\.o"; then
        check 0 "src/${frontend}.cpp is LINKED INTO qt_entry_tests, so it is executed off Windows and not merely compiled"
    else
        check 1 "src/${frontend}.cpp is linked into qt_entry_tests (not on the link line -- has it become compile-only?)"
    fi
done

# The APPLICATION has to link, not just compile. src/main_qt.cpp is on the manifest,
# so dropping the tinta_qt target would show up as MISSING -- but moving main_qt.cpp
# into portable_compile_only would satisfy the manifest exactly while leaving the port
# with no runnable frontend at all. Only the link line tells those apart, and the whole
# goal -- one stack on every platform -- is the thing that quietly stops being true.
if grep -qE -- '-o tinta_qt( |$)' "$BUILD_DIR.build.log"; then
    check 0 "tinta_qt LINKS off Windows, so the Qt frontend is a runnable binary and not a compile-only file set"
else
    check 1 "tinta_qt links off Windows (no link line found -- was the target dropped, or main_qt.cpp made compile-only?)"
fi

note 'Tests'
# ctest exits 0 on an EMPTY selection, so the count is asserted before the
# result. Without this, a configure that produced no tests at all would report a
# clean pass.
test_names="$("$CMAKE_BIN" -E chdir "$BUILD_DIR" ctest -N 2>/dev/null \
    | sed -nE 's/^ +Test +#[0-9]+: +([^[:space:]]+).*/\1/p' | sort -u)"
n_tests="$(printf '%s\n' "$test_names" | grep -c . || true)"
echo "tests configured: ${n_tests:-0}"
printf '%s\n' "$test_names" | sed 's/^/       /'
if [ "${n_tests:-0}" -ge 1 ] 2>/dev/null; then
    check 0 "CONTROL: at least one test is selected, so a pass below is not an empty run"
else
    check 1 "CONTROL: at least one test is selected, so a pass below is not an empty run"
fi

"$CMAKE_BIN" -E chdir "$BUILD_DIR" ctest --output-on-failure > "$BUILD_DIR.ctest.log" 2>&1
check "$?" "every configured test passes off Windows"
tail -25 "$BUILD_DIR.ctest.log"

# Name the tests that must have RUN, not merely configured. "N tests passed" is
# a count, and a count cannot tell you which N.
#
# The names are DERIVED from ctest -N rather than hand-written, for the same
# reason the source manifest is compared both ways. The hand-written version
# listed four -- settings_roundtrip, i18n, platform_paths, keymap -- and three
# more (math_parser, mermaid_parser, document_types) had been running off Windows
# for some time without this section ever mentioning them. A green probe was
# therefore silent about the largest portable test in the tree.
while IFS= read -r test_name; do
    [ -n "$test_name" ] || continue
    if grep -qE "Test +#[0-9]+: ${test_name} .*Passed" "$BUILD_DIR.ctest.log"; then
        check 0 "the ${test_name} test ran and passed off Windows"
    else
        check 1 "the ${test_name} test ran and passed off Windows (not found as Passed in the ctest log)"
    fi
done <<< "$test_names"

note 'Result'
if [ "$failures" = "0" ]; then
    echo "PASS: tinta's portable core builds and tests clean with $CXX_BIN."
    exit 0
fi
echo "$failures assertion(s) failed."
exit 1

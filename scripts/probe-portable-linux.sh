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

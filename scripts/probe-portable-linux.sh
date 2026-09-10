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
# was silently skipped also succeeds. Each file below must appear as its own
# compile line, which is the only thing that shows this compiler read it.
note 'Per-object evidence'
for object in i18n config_dir platform_paths_posix keymap; do
    if grep -qE "(^|[/ ])${object}\.cpp\.o" "$BUILD_DIR.build.log"; then
        check 0 "src/${object}.cpp was compiled by $CXX_BIN"
        grep -oE "[^ ]*${object}\.cpp\.o" "$BUILD_DIR.build.log" | head -1 | sed 's/^/       /'
    else
        check 1 "src/${object}.cpp was compiled by $CXX_BIN (no object line found)"
    fi
done

# Nothing Windows-only may have been dragged in. main_d2d.cpp and the app target
# are inside if(WIN32) and must not appear at all.
if grep -qE "main_d2d\.cpp\.o" "$BUILD_DIR.build.log"; then
    check 1 "the Windows-only app target stayed out of this build"
else
    check 0 "the Windows-only app target stayed out of this build"
fi

note 'Tests'
# ctest exits 0 on an EMPTY selection, so the count is asserted before the
# result. Without this, a configure that produced no tests at all would report a
# clean pass.
test_list="$("$CMAKE_BIN" -E chdir "$BUILD_DIR" ctest -N 2>/dev/null | grep -cE '^ +Test +#[0-9]+:')"
echo "tests configured: ${test_list:-0}"
if [ "${test_list:-0}" -ge 1 ] 2>/dev/null; then
    check 0 "CONTROL: at least one test is selected, so a pass below is not an empty run"
else
    check 1 "CONTROL: at least one test is selected, so a pass below is not an empty run"
fi

"$CMAKE_BIN" -E chdir "$BUILD_DIR" ctest --output-on-failure 2>&1 | tail -25
check "${PIPESTATUS[0]}" "every configured test passes off Windows"

note 'Result'
if [ "$failures" = "0" ]; then
    echo "PASS: tinta's portable core builds and tests clean with $CXX_BIN."
    exit 0
fi
echo "$failures assertion(s) failed."
exit 1

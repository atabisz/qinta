#!/usr/bin/env bash
#
# Census: WHY does each src/*.cpp that is not on the portable manifest fail to
# compile off Windows? One row per file, grouped by cause.
#
# THIS IS A MEASUREMENT, NOT A GATE. Exit 0 means "the rig is trustworthy and
# here is the reading" -- it does NOT mean the tree is portable. The exit status
# counts CONTROL failures only. Read the table.
#
# ---------------------------------------------------------------------------
# Why this script exists, and why it is committed
#
# A census of exactly this shape was run once before and produced the figure
# "render.cpp: 38 errors", which then got quoted for several spans. That rig was
# a directory of 21 hand-written stub headers, built in a scratch directory and
# never committed -- so when the scratch was deleted the figure became
# unreproducible: not wrong, just no longer re-derivable, which is worse,
# because a number nobody can re-take still reads like a measurement. Every
# figure this script prints can be re-taken by running this script.
#
# It also does NOT use stub headers, and that is the substantive difference. A
# stub rig measures a tree against headers the author invented, so its error
# counts are a property of the stubs as much as of the code. This lifts the REAL
# compile command the real portable build used for a real portable file out of
# the verbose build log, substitutes the source file, and adds -fsyntax-only.
# The flags are therefore the flags that a future port of the file would
# actually get, including Qt's include paths and this tree's -std=gnu++17.
#
# The template target is qinta on purpose: it is the Qt frontend, so it is
# the target these files have to land in for the port to mean anything. Reading
# flags from a test target would measure a slightly different world.
#
# THE TEMPLATE IS THE FALLBACK, NOT THE RULE, and that is a correction. A file the
# portable build already compiles is measured with ITS OWN lifted command; the template
# is used only for files that have no command of their own, which is the entire
# non-portable census by definition. See flags_for below for the reading that forced the
# change -- one manifest file was being failed by qinta's include path rather than by
# anything about the file.
#
# ---------------------------------------------------------------------------
# Controls, and why each one is here
#
# Six. Four run before the census and gate everything; 5 and 6 belong to --control and
# gate its table. Any failure makes the census unreadable rather than merely suspect:
#
#   1. The template's OWN source must pass. If the lifted flags cannot compile
#      the file the real build compiled with them, the rig is broken and every
#      error below would be an artefact of the rig.
#   2. <windows.h> must FAIL. Same argument as probe-portable-linux.sh's
#      negative control, and it needs restating here rather than inheriting:
#      these flags are not those flags, and an -isystem path that dragged in a
#      MinGW header set would make every clean result meaningless.
#   3. A file with three independent undeclared identifiers must report exactly
#      3 errors. This is the only control on the COUNTING, as opposed to the
#      pass/fail. A count is the whole output of this script, and an exit code
#      cannot tell 1 error from 300.
#   4. A missing header must be reported as FATAL. This is the sharpest one.
#      g++ stops at a fatal error, so a file whose first problem is a missing
#      header reports a tiny error count that means "not measured", not "nearly
#      portable". Unless fatal is detected and printed, the census reads exactly
#      backwards for the files that are furthest away.
#   5. (--control only) Every file on the portable manifest must come out clean.
#      The positive control at scale: it is what catches a rig that has quietly
#      started reporting errors for everything.
#   6. (--control only) Most manifest rows must have resolved their OWN compile
#      command rather than falling back to the template. CONTROL 5 got easier when
#      per-file flags arrived, and a control that gets easier unnoticed is worse than
#      none -- this is what stops it degrading silently back to what it was.
#
# Run from anywhere:
#   scripts/census-nonportable-linux.sh                # the whole census
#   scripts/census-nonportable-linux.sh --file src/render.cpp
#   scripts/census-nonportable-linux.sh --errors src/render.cpp   # full list
#   scripts/census-nonportable-linux.sh --control      # census the PORTABLE set
#
# --control inverts the subject: it runs the manifest files through the same
# path, where every one must come out clean. It is the positive control at
# scale, and it is what catches a rig that has quietly started reporting errors
# for everything.
#
# Requires a build log from a completed verbose Linux build, which is what
# probe-portable-linux.sh leaves at "$BUILD_DIR.build.log". This script will NOT
# build one itself: a census that silently reconfigured and rebuilt would be
# reading flags it had just created, and the failure mode -- a stale or partial
# build producing a plausible-looking template -- is exactly the kind of quiet
# wrong reading the whole file is written against.

set -u -o pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TINTA_LINUX_BUILD_DIR:-${HOME}/.cache/tinta-linux-build}"
MANIFEST="$SOURCE_DIR/tests/portable-sources.txt"
TEMPLATE_SOURCE="src/markdown.cpp"   # on the manifest, compiled by qinta
TEMPLATE_OBJECT="CMakeFiles/qinta.dir/src/markdown.cpp.o"

mode="census"
only_file=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --file)      only_file="$2"; shift 2 ;;
        --errors)    mode="errors"; only_file="$2"; shift 2 ;;
        --control)   mode="control"; shift ;;
        -h|--help)   sed -n '2,80p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

BUILD_LOG="$BUILD_DIR.build.log"
control_failures=0
check() {
    if [ "$1" = "0" ]; then
        printf 'ok   : %s\n' "$2"
    else
        printf 'FAIL : %s\n' "$2"
        control_failures=$((control_failures + 1))
    fi
}

printf '== Rig\n'
echo "source    : $SOURCE_DIR"
echo "build log : $BUILD_LOG"
if [ ! -f "$BUILD_LOG" ]; then
    echo "REFUSED: no build log. Run scripts/probe-portable-linux.sh first, or pass --build-dir."
    exit 3
fi

# --- The template compile command ------------------------------------------
# Lifted whole, then stripped of the four flags that name the output. Nothing is
# added except -fsyntax-only, so the include paths, the standard, the Qt
# -isystem dirs and the defines are all the real build's.
template="$(grep -F -- "-o $TEMPLATE_OBJECT -c" "$BUILD_LOG" | head -1)"
if [ -z "$template" ]; then
    echo "REFUSED: no compile line for $TEMPLATE_OBJECT in the build log."
    echo "         Was the build verbose (VERBOSE=1), and did qinta configure?"
    exit 3
fi

# Drop the compiler, the dependency-file flags and the output/input pair. Nothing
# else -- -fPIC stays, so "nothing removed except output naming" is literally true.
#
# A function rather than a straight-line loop because the template is no longer the
# only compile line this script lifts: see flags_for below.
LIFTED=()
lift_from_line() {
    LIFTED=()
    local skip_next=0 first=1 tok
    for tok in $1; do
        if [ "$skip_next" = "1" ]; then skip_next=0; continue; fi
        if [ "$first" = "1" ]; then first=0; continue; fi   # /usr/bin/g++
        case "$tok" in
            -MD) continue ;;
            -MT|-MF|-o) skip_next=1; continue ;;
            -c) skip_next=1; continue ;;                     # -c <source>
            *) LIFTED+=("$tok") ;;
        esac
    done
}

lift_from_line "$template"
FLAGS=("${LIFTED[@]}")
CXX_BIN="$(printf '%s' "$template" | awk '{print $1}')"

echo "compiler  : $CXX_BIN"
"$CXX_BIN" --version | head -1
echo "flags     : ${FLAGS[*]}"

# Refusals on the shape of the lifted flags. A template that silently lost its
# include path would make every file fail at the first tinta header, and the
# census would read as "the whole tree needs Windows".
have_include=0; have_std=0
for f in "${FLAGS[@]}"; do
    case "$f" in
        -I"$SOURCE_DIR"/include) have_include=1 ;;
        -std=*) have_std=1 ;;
    esac
done
if [ "$have_include" != "1" ]; then
    echo "REFUSED: the lifted flags do not include -I$SOURCE_DIR/include."
    exit 3
fi
if [ "$have_std" != "1" ]; then
    echo "REFUSED: the lifted flags name no -std=, so this is not the real build's command."
    exit 3
fi

# PER-FILE FLAGS, AND WHY THE TEMPLATE ALONE WAS NOT ENOUGH.
#
# One template's flags cannot stand in for the whole tree, and the failure was not
# hypothetical: src/image_qt.cpp is on the portable manifest, includes
# QNetworkAccessManager and QSvgRenderer, and is the only source in the build compiled
# into image_tests rather than qinta -- so it gets -isystem .../QtNetwork and
# .../QtSvg, which qinta's flags do not carry. Measured against the template it read
# `fatal 1, QNetworkAccessManager: No such file or directory`, which is a fact about
# qinta's include path and nothing at all about that file's portability. It compiles
# cleanly in the real build, every day.
#
# So: a file that the PORTABLE build actually compiles is measured with ITS OWN lifted
# command; a file the portable build never touches has no line to lift and falls back to
# the template. That fallback covers the entire non-manifest census by definition -- those
# files are excluded from the portable build, which is why they are the subject -- so the
# main table is unaffected by this change and the control run becomes a real control.
#
# The origin is reported per row rather than assumed, because "which flags measured this"
# is exactly the thing that was silently wrong before.
flags_for() {
    # $1 = repo-relative source path. Sets FILE_FLAGS and flags_origin.
    local own
    own="$(grep -F -- "/$(basename "$1").o -c" "$BUILD_LOG" | head -1)"
    if [ -n "$own" ]; then
        lift_from_line "$own"
        FILE_FLAGS=("${LIFTED[@]}")
        flags_origin="own"
    else
        FILE_FLAGS=("${FLAGS[@]}")
        flags_origin="tmpl"
    fi
}

syntax_only() {
    # Prints nothing; leaves diagnostics in $DIAG and the exit status in $rc.
    # $2, when given, is the repo-relative path used to look up per-file flags.
    if [ -n "${2:-}" ]; then
        flags_for "$2"
    else
        FILE_FLAGS=("${FLAGS[@]}")
        flags_origin="tmpl"
    fi
    "$CXX_BIN" "${FILE_FLAGS[@]}" -fsyntax-only "$1" > "$DIAG" 2>&1
    rc=$?
}

DIAG="$(mktemp)"
TMPD="$(mktemp -d)"
trap 'rm -f "$DIAG"; rm -rf "$TMPD"' EXIT

n_errors() { grep -c ': error:' "$DIAG" || true; }
n_fatal()  { grep -c ': fatal error:' "$DIAG" || true; }
first_cause() {
    # The first diagnostic line, normalised to the message. A fatal is reported
    # as fatal, because that is the difference between a reading and a stop.
    local line
    line="$(grep -m1 -E ': (fatal error|error):' "$DIAG" || true)"
    if [ -z "$line" ]; then echo '(none)'; return; fi
    printf '%s' "$line" | sed -E 's#^.*: (fatal error|error): #\1: #'
}

printf '\n== Controls\n'

# 1. The template's own source.
syntax_only "$SOURCE_DIR/$TEMPLATE_SOURCE"
if [ "$rc" = "0" ]; then
    check 0 "CONTROL 1: the lifted flags compile $TEMPLATE_SOURCE, the file the real build compiled with them"
else
    check 1 "CONTROL 1: the lifted flags compile $TEMPLATE_SOURCE (they do NOT -- the rig is broken, ignore everything below)"
    head -5 "$DIAG" | sed 's/^/       /'
fi

# 2. windows.h must not resolve.
printf '#include <windows.h>\nint main() { return 0; }\n' > "$TMPD/ctl_win.cpp"
syntax_only "$TMPD/ctl_win.cpp"
if [ "$rc" != "0" ]; then
    check 0 "CONTROL 2: <windows.h> does not resolve under these flags, so a clean result below means something"
else
    check 1 "CONTROL 2: <windows.h> does not resolve (it COMPILED -- this environment is not a valid witness)"
fi

# 3. The error COUNT, not the exit code.
printf 'void f() { alpha_undeclared; beta_undeclared; gamma_undeclared; }\n' > "$TMPD/ctl_count.cpp"
syntax_only "$TMPD/ctl_count.cpp"
ctl_count="$(n_errors)"
ctl_count_fatal="$(n_fatal)"
if [ "$ctl_count" = "3" ] && [ "$ctl_count_fatal" = "0" ]; then
    check 0 "CONTROL 3: three planted errors are counted as exactly 3, fatal 0 -- the tally is instrumented, not guessed"
else
    check 1 "CONTROL 3: three planted errors count as 3 (got errors=$ctl_count fatal=$ctl_count_fatal)"
    head -8 "$DIAG" | sed 's/^/       /'
fi

# 4. Fatal detection. Without this the files furthest from portable read as the
#    closest, because g++ stops counting at the first missing header.
printf '#include <tinta_no_such_header_xyz.h>\nint main() { return 0; }\n' > "$TMPD/ctl_fatal.cpp"
syntax_only "$TMPD/ctl_fatal.cpp"
ctl_fatal="$(n_fatal)"
if [ "$ctl_fatal" -ge 1 ] 2>/dev/null; then
    check 0 "CONTROL 4: a missing header is reported as FATAL, so a small error count can be told from a stopped read"
else
    check 1 "CONTROL 4: a missing header is reported as fatal (fatal lines = $ctl_fatal)"
    head -5 "$DIAG" | sed 's/^/       /'
fi

# --- Subject set ------------------------------------------------------------
if [ ! -f "$MANIFEST" ]; then
    echo "REFUSED: $MANIFEST is missing -- it is what defines the non-portable set."
    exit 3
fi
manifest_files="$(grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | tr -d '\r' | sed 's#^#src/#' | sort -u)"
all_files="$(cd "$SOURCE_DIR" && ls src/*.cpp | sort -u)"

case "$mode" in
    control) subject="$manifest_files"; subject_label="the PORTABLE manifest (every row must be clean)" ;;
    *)       subject="$(comm -23 <(printf '%s\n' "$all_files") <(printf '%s\n' "$manifest_files"))"
             subject_label="src/*.cpp NOT on the portable manifest" ;;
esac
if [ -n "$only_file" ]; then
    subject="$only_file"
    subject_label="$only_file"
fi

if [ "$mode" = "errors" ]; then
    printf '\n== Every diagnostic for %s\n' "$only_file"
    syntax_only "$SOURCE_DIR/$only_file" "$only_file"
    echo "exit=$rc errors=$(n_errors) fatal=$(n_fatal) flags=$flags_origin"
    echo '--- diagnostics ---'
    cat "$DIAG"
    printf '\n== Distinct causes, most frequent first\n'
    grep -E ': (fatal error|error):' "$DIAG" \
        | sed -E 's#^.*: (fatal error|error): #\1: #' \
        | sed -E "s#'[^']*'#'X'#g" \
        | sort | uniq -c | sort -rn
    printf '\n== Result\n'
    echo "control failures: $control_failures"
    [ "$control_failures" = "0" ] && exit 0 || exit 1
fi

n_subject="$(printf '%s\n' "$subject" | grep -c . || true)"
printf '\n== Census: %s (%d files)\n' "$subject_label" "$n_subject"
if [ "$n_subject" = "0" ]; then
    check 1 "CONTROL: the subject set is non-empty (an empty census prints a clean table and measures nothing)"
fi

# fatal comes BEFORE errors in the column order deliberately. A reader scanning
# for the biggest number should hit the "was this even measured" column first.
printf '%-28s %5s %5s %7s %5s  %s\n' 'file' 'exit' 'fatal' 'errors' 'flags' 'first cause'
causes_tmp="$(mktemp)"
clean=0; dirty=0; n_own=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    syntax_only "$SOURCE_DIR/$f" "$f"
    e="$(n_errors)"; fa="$(n_fatal)"; c="$(first_cause)"
    printf '%-28s %5s %5s %7s %5s  %s\n' "${f#src/}" "$rc" "$fa" "$e" "$flags_origin" "$c"
    if [ "$rc" = "0" ]; then clean=$((clean + 1)); else dirty=$((dirty + 1)); fi
    [ "$flags_origin" = "own" ] && n_own=$((n_own + 1))
    printf '%s\n' "$c" | sed -E "s#'[^']*'#'X'#g" >> "$causes_tmp"
done <<< "$subject"

printf '\nclean: %d   fails: %d\n' "$clean" "$dirty"

printf '\n== Distinct causes across the census, most frequent first\n'
sort "$causes_tmp" | uniq -c | sort -rn
rm -f "$causes_tmp"

if [ "$mode" = "control" ]; then
    if [ "$dirty" = "0" ]; then
        check 0 "CONTROL 5: every file on the portable manifest compiles under its own lifted flags"
    else
        check 1 "CONTROL 5: every file on the portable manifest compiles under its own lifted flags ($dirty failed)"
    fi

    # CONTROL 6 exists because CONTROL 5 got easier, and a control that got easier
    # without being noticed is worse than no control. The per-file lookup above could
    # silently degrade to the template for every row -- a renamed object path, a
    # non-verbose build, a typo in the grep -- and CONTROL 5 would then be measuring
    # exactly what it measured before while claiming to measure something better.
    # The manifest is by definition the set the portable build compiles, so nearly
    # every row must resolve to its own command; "most of them" is the assertable
    # version of that.
    if [ "$n_own" -gt $((n_subject / 2)) ]; then
        check 0 "CONTROL 6: the per-file lookup resolved real commands ($n_own of $n_subject rows), so CONTROL 5 is not the template in disguise"
    else
        check 1 "CONTROL 6: the per-file lookup resolved real commands (only $n_own of $n_subject rows -- CONTROL 5 has quietly reverted to template flags)"
    fi
fi

printf '\n== Result\n'
echo "control failures: $control_failures"
if [ "$control_failures" = "0" ]; then
    echo "CENSUS VALID: the controls hold, so the table above is a reading. It is not a pass."
    exit 0
fi
echo "CENSUS INVALID: $control_failures control(s) failed -- do not quote the table."
exit 1

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
# WHAT `clean` USED TO MEAN, AND WHY THERE IS NOW A THIRD VERDICT
#
# Until 2026-09-23 this census had two outcomes per row, clean and fails, and `clean`
# meant two opposite things. src/tab_ghost_win32.cpp opens `#ifdef _WIN32` on line 3
# and closes `#endif` on its last line; src/zen_mode_win32.cpp does the same. Off
# Windows each of those compiles to an EMPTY TRANSLATION UNIT -- and this script scored
# that exit 0, fatal 0, errors 0, and tallied it as clean. For those two rows "clean"
# was a reading that the file is maximally UNportable: there is no portable code in it,
# because there is no code in it.
#
# The third clean row, src/settings_app.cpp, is the genuine article -- no conditionals
# anywhere, a documented deliberate manifest exclusion -- so the column was not simply
# wrong. It was two populations under one word, which is worse, and it got that way by
# ADDITION rather than by error: the word was accurate while every clean row was a real
# one, and it rotted when the tree gained files that are wholly guarded.
#
# So a row now also reports what the compile EMITTED, and `clean` excludes the ones
# that emitted nothing:
#
#   emits=code   the object carries code or exports a symbol -- a real reading
#   emits=NONE   the translation unit was empty; tallied `vacuous`, never `clean`
#   emits=?      UNDECIDABLE. Never read as either (see below)
#
# THE VERDICT IS TWO READINGS THAT MUST BOTH BE ZERO, AND THAT IS MEASURED RATHER THAN
# CAUTIOUS. Seven synthetic shapes were compiled before any of this was written, and
# the two candidate routes -- `nm --defined-only --extern-only` and the object's .text
# size -- disagree on two of them in OPPOSITE directions:
#
# All seven are listed, not the five that make the point, because the two omitted rows
# are exactly the two blind spots -- and a table that says "seven shapes" while showing
# five is how a decomposition stops matching the total it is quoted under.
#
#   shape                          extern  .text   both-zero   either route ALONE
#   everything inside the guard        0      0     NONE  ok    vacuous (correct)
#   guard around the body only,
#     #include left outside it        0      0     NONE  ok    vacuous (correct; the
#                                                  header's three .rodata symbols are
#                                                  local, so --extern-only is not fooled)
#   one exported function             1     15     code  ok    code (correct)
#   only a STATIC function            0     15     code  ok    extern alone: vacuous. WRONG.
#   only namespace-scope data         1      0     code  ok    .text alone: vacuous. WRONG.
#   only an UNINSTANTIATED template   0      0     NONE  MISS  both say vacuous
#   only `const` namespace-scope
#     data (goes to .rodata)          0      0     NONE  MISS  both say vacuous
#
# So both-zero is right on five of seven and there are TWO blind spots, stated rather
# than guarded, both in the same direction -- they read `vacuous` when the file does
# carry portable source:
#
#   * .rodata is deliberately ignored, because a HEADER contributes it: the static-only
#     shape above picked up three `r` symbols from <string>. Counting .rodata would have
#     cost the guard-around-the-body row, which is a real defect shape. So a file whose
#     sole contribution is `const` namespace-scope data reads vacuous.
#   * an uninstantiated template emits nothing at all by design, so no section or symbol
#     reader can see it. This one cannot be fixed by choosing a different section.
#
# Neither shape is reachable in src/*.cpp today, and that is a MEASUREMENT, not a
# reassurance: a file with either shape would read NONE, --control reads 0 vacuous across
# all 68 manifest rows, and the only two NONE rows in the census are accounted for by a
# guard spanning the whole file (tab_ghost_win32.cpp and zen_mode_win32.cpp, `#ifdef
# _WIN32` on line 3 to `#endif` on the last line). UNTIL 2026-09-23 NOTHING NOTICED a
# future file of either shape, and this paragraph said reading such a row was a person's
# job. It is now guard_explains: a third reader, of the SOURCE rather than the object, that
# accepts a NONE row only when the file is one whole-file `#ifdef _WIN32` guard with nothing
# but `#include`s outside it. Any other NONE row fails the census by name (CONTROLS 10-14
# prove it can answer both ways; a planted template-only src/ file turned the census INVALID).
# It still cannot tell you WHAT the unexplained file is -- that is still a person's read.
#
# Both misses are FALSE vacuous, never false clean -- the direction that gets a row
# looked at rather than skipped.
#
# AND NEITHER ROUTE MAY BE READ OVER AN OBJECT THAT IS NOT THERE. `nm` over a missing
# file counts zero and puts its only complaint on stderr; `size`'s field comes back
# empty. Both of those are "vacuous" if you are not looking. So the object is asserted
# present and non-empty first, `size -A` must print its Total line to prove the section
# reader ran at all, and anything short of that is `?` -- which makes the census exit
# non-zero rather than quietly becoming a verdict.
#
# ---------------------------------------------------------------------------
# Controls, and why each one is here
#
# Fourteen. Eleven run before the census and gate everything -- 1-4, 7-8 and 10-14; 5 and
# 6 belong to --control and gate its table, and 9 is a claim about the tree that only
# --control can ask. (This said "Eight" while listing nine; it was not re-counted when 9
# landed.) THE NUMBERS ARE ADDITION ORDER, NOT EXECUTION ORDER, deliberately:
# CMakeLists.txt cites "CONTROL 5" by number, so renumbering to make the sequence
# read top-to-bottom would rot a citation to buy nothing. Any failure among the eleven
# makes the census unreadable rather than merely suspect:
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
#   7. A file whose whole body sits inside `#ifdef _WIN32` must read emits=NONE. The
#      vacuity probe's positive control: without it, a broken object path or a missing
#      `nm` reads as "every translation unit is empty" and the new column is worse than
#      no column, because it agrees with the hypothesis that motivated it.
#   8. A file defining one exported function must read emits=code. The counter-case, and
#      it is the arm that has to exist for CONTROL 7's green to mean anything: a probe
#      that answers NONE for everything passes 7 and fails 8.
#   9. (--control only) No file on the portable manifest may read emits=NONE. This one
#      is a claim about the TREE rather than about the rig -- a file claimed portable
#      that compiles to nothing off Windows is a defect in the manifest, not a broken
#      instrument -- and it sits here with CONTROL 5, which is the same kind of claim.
#  10-14. The guard reader (guard_explains). 10 and 12 must read explained: CONTROL 7's
#      file, and the real files' shape with an #include outside the guard. 11, 13 and 14
#      must read unexplained: a template-only file (first shown to read emits=NONE, so it
#      is the blind spot and not a file that never reaches this reader), a guard that
#      closes early though the last line is #endif, and a guard with a top-level #else.
#      A reader stuck on either answer fails one side; both mutants were run.
#
# ---------------------------------------------------------------------------
# A THIRD THING THIS CENSUS CANNOT SEE: COUPLING CARRIED BY A PARAMETER TYPE
#
# The two already stated are the cause table's two categories and D9's type-level
# fstream assumption. This one is worth stating because it ranks files BACKWARDS,
# and it was measured rather than reasoned about.
#
# src/print.cpp's drawDocumentRange was 222 lines that were 100% unportable, for
# one reason: it took an ID2D1DeviceContext*. Every drawing call inside it spelled
# that coupling `dc->FillRectangle(...)` and `brush->SetColor(...)` -- and neither
# `dc` nor `brush` matches any pattern an identifier grep can use, because they are
# variable names. So at base 4eda78a the function read TWELVE identifiers under the
# pattern the README quotes, while the print plumbing that is deliberately NOT being
# ported read TWENTY-EIGHT (printDebugPages) and TWENTY-FIVE (the preview
# rasteriser). A reader ranking work by this census would have started with the
# device plumbing and concluded the drawing function barely touched Direct2D.
#
# Retargeting that one parameter to tinta::RenderTarget& took the function's count
# to ZERO with no change to what it draws -- the pages are byte-identical -- and
# took the whole file from 90 live to 78. The delta of 12 is the honest figure and
# it UNDERSTATES the change by roughly twenty to one in lines.
#
# The rule that follows: a low count means the Direct2D types are not SPELLED in
# this file's body. It does not mean the code is nearly portable, and for a function
# that receives its device as an argument it means almost the opposite. To find that
# class, read the SIGNATURES, not the bodies.
#
# The mention count (comments included) is worse for this purpose, not better: it
# read 93 before the port and 92 after, because the comments recording what changed
# re-add the very tokens the code stopped using.
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
        # WAS sed -n '2,80p', AND THE PARAMETER-TYPE NOTE ABOVE BROKE IT SILENTLY.
        # A hardcoded last line is a claim about where the header ends, and adding to
        # the header is the one edit guaranteed to falsify it -- --help would have
        # stopped mid-sentence with no error anywhere. This prints the header by its
        # SHAPE instead: every comment line after the shebang, stopping at the first
        # line that is not one. Same defect class as every stale count this file
        # warns about, in the file's own help.
        -h|--help)   awk 'NR == 1 { next } /^#/ { print; next } { exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
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

# The two object readers the emits column depends on. A REFUSAL rather than a skipped
# column, because both of them count zero over nothing they can read: with nm absent,
# `nm ... | grep -c .` is 0 and every row reads emits=NONE with confidence.
NM_BIN="$(command -v nm || true)"
SIZE_BIN="$(command -v size || true)"
if [ -z "$NM_BIN" ] || [ -z "$SIZE_BIN" ]; then
    echo "REFUSED: the emits column needs binutils nm and size on PATH (nm='$NM_BIN' size='$SIZE_BIN')."
    echo "         Both count zero over an object they cannot read, so a missing one would"
    echo "         report every translation unit as empty."
    exit 3
fi
echo "readers   : $NM_BIN, $SIZE_BIN"

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

# Did the compile EMIT anything? Reasoning, the seven-shape table and the blind spot are
# all in the header; this is the mechanism.
#
# It is a SECOND compile, on rows that already read exit 0, and not a switch of the first
# one from -fsyntax-only to -c. That is deliberate and it is a claim-17 point: every
# fatal/errors figure README publishes was measured with -fsyntax-only, codegen can add
# diagnostics syntax checking never sees, and moving the primary reading would void
# readings this change is not re-taking. Rows that already fail cost nothing here,
# because a compile that stopped never got to codegen anyway.
vac_verdict=''; vac_extern=''; vac_text=''; vac_note=''
vacuity_of() {
    # $1 = source path to compile. Reads FILE_FLAGS, which the syntax_only() call
    # immediately before has already resolved for this file -- the same flags, so the
    # emits reading and the errors reading are about the same compile world.
    local obj="$TMPD/vac.o" total
    vac_extern='-'; vac_text='-'; vac_note=''
    rm -f "$obj"

    if ! "$CXX_BIN" "${FILE_FLAGS[@]}" -c -o "$obj" "$1" > "$TMPD/vac.err" 2>&1; then
        vac_verdict='?'
        vac_note='the -c compile failed where -fsyntax-only passed, so codegen saw something syntax checking did not'
        return
    fi
    if [ ! -s "$obj" ]; then
        vac_verdict='?'
        vac_note='no object file on disk after a successful -c, so neither reader has anything to read'
        return
    fi

    # Route 1: what a linker could resolve out of this object. --extern-only is what
    # makes a header's own .rodata constants not count as this file's contribution.
    vac_extern="$("$NM_BIN" --defined-only --extern-only "$obj" 2>/dev/null | grep -c . || true)"

    # Route 2: whether any code was emitted. The Total line is checked FIRST and for its
    # own sake: it is the evidence that the section reader ran on a real object, which is
    # what then licenses reading an ABSENT .text row as zero bytes rather than as
    # "size said nothing".
    total="$("$SIZE_BIN" -A "$obj" 2>/dev/null | grep -c '^Total' || true)"
    if [ "$total" = "0" ]; then
        vac_verdict='?'
        vac_note="size -A printed no Total line for $obj, so the section reader did not run"
        return
    fi
    vac_text="$("$SIZE_BIN" -A "$obj" 2>/dev/null | awk '$1 == ".text" { print $2 }')"
    [ -n "$vac_text" ] || vac_text=0

    if [ "$vac_extern" = "0" ] && [ "$vac_text" = "0" ]; then
        vac_verdict='NONE'
    else
        vac_verdict='code'
    fi
}

# Does a WHOLE-FILE Windows guard explain an emits=NONE reading? Exit 0 if it does.
#
# This is what the two blind spots in the header need, and it is a reading of the SOURCE, not
# of the object -- deliberately a third route, because both object routes are blind in the same
# direction. The rule is the shape both real vacuous files measure, not a paraphrase of it:
# tab_ghost_win32.cpp and zen_mode_win32.cpp open with `#include "<own>_win32.h"` OUTSIDE the
# guard, then `#ifdef _WIN32` on line 3, then `#endif  // _WIN32` as the last line. So, after
# comments and blank lines are removed:
#
#   * every line outside the guard is an `#include` -- a header can put nothing in this object
#     that the object readers would miss, since the object already read NONE;
#   * there is exactly one top-level `#ifdef _WIN32` / `#if defined(_WIN32)`, and its MATCHING
#     `#endif` is the last significant line. Matching is tracked by depth, because "the last
#     line is #endif" also describes a guard that closed early with a template after it;
#   * that guard has no top-level `#else`/`#elif`, whose branch would be the non-Windows code.
#
# Anything else -- including a file that really is vacuous for a reason this rule does not
# describe -- is UNEXPLAINED, and the census refuses to be quoted until a person has read it.
# That is the direction the blind spots need: a false "unexplained" costs a look, a false
# "explained" would hide exactly the file the header warns about.
guard_explains() {
    tr -d '\r' < "$1" | awk '
        {
            line = $0; out = ""
            while (length(line) > 0) {
                if (inblk) {
                    i = index(line, "*/")
                    if (!i) { line = ""; break }
                    line = substr(line, i + 2); inblk = 0
                    continue
                }
                i = index(line, "/*"); j = index(line, "//")
                if (j && (!i || j < i)) { out = out substr(line, 1, j - 1); break }
                if (i) { out = out substr(line, 1, i - 1); line = substr(line, i + 2); inblk = 1 }
                else   { out = out line; line = "" }
            }
            gsub(/^[ \t]+|[ \t]+$/, "", out)
            if (out == "") next
            if (closed) { bad = 1; exit }
            if (!opened) {
                if (out ~ /^#[ \t]*include[ \t]*[<"]/) next
                if (out ~ /^#[ \t]*ifdef[ \t]+_WIN32$/ ||
                    out ~ /^#[ \t]*if[ \t]+defined[ \t]*\(?[ \t]*_WIN32[ \t]*\)?$/) {
                    opened = 1; depth = 1; next
                }
                bad = 1; exit
            }
            if (out ~ /^#[ \t]*if/) { depth++; next }
            if (depth == 1 && out ~ /^#[ \t]*(else|elif)/) { bad = 1; exit }
            if (out ~ /^#[ \t]*endif/) { depth--; if (depth == 0) closed = 1 }
        }
        END { exit (bad || !closed) ? 1 : 0 }'
}

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

# 7. The vacuity probe's positive control: the real defect shape must read NONE.
#    The include is INSIDE the guard on purpose -- that is the shape of the two files
#    that motivated the column.
printf '// only a comment out here\n#ifdef _WIN32\n#include <string>\nint ctl_guarded() { return 1; }\n#endif\n' > "$TMPD/ctl_vac_none.cpp"
FILE_FLAGS=("${FLAGS[@]}"); flags_origin="tmpl"
vacuity_of "$TMPD/ctl_vac_none.cpp"
if [ "$vac_verdict" = "NONE" ]; then
    check 0 "CONTROL 7: a file wholly inside #ifdef _WIN32 reads emits=NONE (extern=$vac_extern .text=$vac_text)"
else
    check 1 "CONTROL 7: a file wholly inside #ifdef _WIN32 reads emits=NONE (got '$vac_verdict' extern=$vac_extern .text=$vac_text ${vac_note})"
fi

# 8. The counter-case, and it is the arm that gives CONTROL 7 its meaning. A probe that
#    answered NONE for everything -- missing nm, wrong object path, an awk field that
#    silently came back empty -- passes 7 and fails only here.
printf 'int ctl_real() { return 1; }\n' > "$TMPD/ctl_vac_code.cpp"
FILE_FLAGS=("${FLAGS[@]}"); flags_origin="tmpl"
vacuity_of "$TMPD/ctl_vac_code.cpp"
if [ "$vac_verdict" = "code" ]; then
    check 0 "CONTROL 8: a file defining one exported function reads emits=code (extern=$vac_extern .text=$vac_text) -- so NONE is not this probe's only answer"
else
    check 1 "CONTROL 8: a file defining one exported function reads emits=code (got '$vac_verdict' extern=$vac_extern .text=$vac_text ${vac_note})"
fi

# 10-14. The guard reader. Two arms that must say "explained" and three that must not; a
# reader that said either answer for everything passes at most one side.
guard_ctl() {
    # $1 = control number, $2 = expected (explained|unexplained), $3 = what the file is.
    local got=unexplained
    guard_explains "$TMPD/ctl_guard_$1.cpp" && got=explained
    if [ "$got" = "$2" ]; then
        check 0 "CONTROL $1: $3 reads $got"
    else
        check 1 "CONTROL $1: $3 reads $2 (got $got)"
    fi
}
cp "$TMPD/ctl_vac_none.cpp" "$TMPD/ctl_guard_10.cpp"
guard_ctl 10 explained "CONTROL 7's file, a comment then a guard around everything,"
# 11 is the blind spot itself, so it must first be shown to READ NONE -- otherwise the
# unexplained answer is about a file that could never reach this reader.
printf 'template <typename T> T ctl_tmpl(T t) { return t; }\n' > "$TMPD/ctl_guard_11.cpp"
FILE_FLAGS=("${FLAGS[@]}"); flags_origin="tmpl"
vacuity_of "$TMPD/ctl_guard_11.cpp"
if [ "$vac_verdict" = "NONE" ]; then
    guard_ctl 11 unexplained "a file of only an uninstantiated template (emits=NONE, the header's blind spot)"
else
    check 1 "CONTROL 11: a template-only file reads emits=NONE before its guard is asked (got '$vac_verdict' ${vac_note})"
fi
printf '#include "x_win32.h"\n\n#ifdef _WIN32\n#include <cstdint>\nint f() { return 1; }\n#endif  // _WIN32\n' > "$TMPD/ctl_guard_12.cpp"
guard_ctl 12 explained "the real files' shape, an #include outside then a guard to the last line,"
printf '#ifdef _WIN32\nint f() { return 1; }\n#endif\ntemplate <typename T> T g(T t) { return t; }\n#ifdef _WIN32\n#endif\n' > "$TMPD/ctl_guard_13.cpp"
guard_ctl 13 unexplained "a guard that closes early with a template after it, though the last line is #endif,"
printf '#ifdef _WIN32\nint f() { return 1; }\n#else\ntemplate <typename T> T g(T t) { return t; }\n#endif\n' > "$TMPD/ctl_guard_14.cpp"
guard_ctl 14 unexplained "a guard with a top-level #else branch"

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
    vac_verdict='-'
    [ "$rc" = "0" ] && vacuity_of "$SOURCE_DIR/$only_file"
    echo "exit=$rc errors=$(n_errors) fatal=$(n_fatal) flags=$flags_origin emits=$vac_verdict"
    [ "$vac_verdict" = "NONE" ] && echo "NOTE: the translation unit is EMPTY off Windows (extern=$vac_extern .text=$vac_text) -- exit 0 here is not portability"
    [ "$vac_verdict" = "?" ] && echo "NOTE: emits is UNDECIDABLE -- $vac_note"
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
# for the biggest number should hit the "was this even measured" column first. `emits`
# is last of the measured columns for the same reason inverted: it qualifies exit 0, so
# it belongs beside the cause a reader lands on rather than up with the tallies.
printf '%-28s %5s %5s %7s %5s %6s  %s\n' 'file' 'exit' 'fatal' 'errors' 'flags' 'emits' 'first cause'
causes_tmp="$(mktemp)"
notes_tmp="$(mktemp)"
clean=0; vacuous=0; dirty=0; undecidable=0; n_own=0
vac_explained=''; vac_unexplained=''
while IFS= read -r f; do
    [ -n "$f" ] || continue
    syntax_only "$SOURCE_DIR/$f" "$f"
    e="$(n_errors)"; fa="$(n_fatal)"; c="$(first_cause)"
    # A row that failed to compile gets no emits reading, and prints '-' rather than
    # NONE. It emitted nothing, but "it emitted nothing" is not the finding there -- the
    # errors are -- and reusing NONE would merge the two populations this column exists
    # to separate.
    if [ "$rc" = "0" ]; then
        vacuity_of "$SOURCE_DIR/$f"
    else
        vac_verdict='-'
    fi
    printf '%-28s %5s %5s %7s %5s %6s  %s\n' "${f#src/}" "$rc" "$fa" "$e" "$flags_origin" "$vac_verdict" "$c"
    case "$rc:$vac_verdict" in
        0:code) clean=$((clean + 1)) ;;
        0:NONE) vacuous=$((vacuous + 1))
                if guard_explains "$SOURCE_DIR/$f"; then
                    vac_explained="$vac_explained ${f#src/}"
                else
                    vac_unexplained="$vac_unexplained ${f#src/}"
                fi ;;
        0:\?)   undecidable=$((undecidable + 1))
                printf '%s: %s\n' "${f#src/}" "$vac_note" >> "$notes_tmp" ;;
        *)      dirty=$((dirty + 1)) ;;
    esac
    [ "$flags_origin" = "own" ] && n_own=$((n_own + 1))
    printf '%s\n' "$c" | sed -E "s#'[^']*'#'X'#g" >> "$causes_tmp"
done <<< "$subject"

printf '\nclean: %d   vacuous: %d   fails: %d' "$clean" "$vacuous" "$dirty"
[ "$undecidable" != "0" ] && printf '   UNDECIDABLE: %d' "$undecidable"
printf '\n'
if [ "$vacuous" != "0" ]; then
    echo "note: 'vacuous' means the file compiled to an EMPTY translation unit off Windows."
    echo "      Those rows exit 0 and are the OPPOSITE of portable. They are not in clean."
    [ -n "$vac_explained" ] && echo "      explained by a whole-file #ifdef _WIN32 guard:$vac_explained"
    [ -n "$vac_unexplained" ] && echo "      UNEXPLAINED -- no whole-file guard, so possibly a blind-spot shape:$vac_unexplained"
fi
if [ "$undecidable" != "0" ]; then
    echo "UNDECIDABLE rows -- the emits reading could not be taken, so it is neither verdict:"
    sed 's/^/      /' "$notes_tmp"
fi
rm -f "$notes_tmp"

# The three tallies must account for every row. Without this, a verdict this script
# forgot to case on would vanish from all three counters and the footer would read as a
# smaller, cleaner census than the one that ran -- a silent instrument built out of an
# unhandled case (claim 24).
if [ "$((clean + vacuous + dirty + undecidable))" != "$n_subject" ]; then
    check 1 "CONTROL: the tallies account for every row (clean $clean + vacuous $vacuous + fails $dirty + undecidable $undecidable != $n_subject censused)"
fi
# An UNDECIDABLE row never passes quietly. It is not a failure of the tree and not a
# reading either, so it fails the RIG: the table must not be quoted until it is resolved.
if [ "$undecidable" != "0" ]; then
    check 1 "CONTROL: every clean row got an emits reading ($undecidable did not -- see the notes above)"
fi
# An UNEXPLAINED vacuous row fails the rig for the same reason. Both object readers miss a
# template-only or const-data-only file, so NONE without a whole-file guard is a verdict this
# script cannot license. Read the file; then either guard it, port it, or extend guard_explains.
if [ -n "$vac_unexplained" ]; then
    check 1 "CONTROL: every vacuous row is explained by a whole-file #ifdef _WIN32 guard (not:$vac_unexplained)"
fi

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

    # CONTROL 9 is a claim about the TREE, and it is the sharper half of CONTROL 5.
    # CONTROL 5 asks whether a manifest row compiles; this asks whether compiling it
    # produced anything. A row that passes 5 and fails 9 is a file listed as portable
    # that contributes an empty object to the portable build -- which is the same defect
    # the emits column was added to catch, sitting on the side of the manifest where it
    # would be a claim rather than a label.
    if [ "$vacuous" = "0" ]; then
        check 0 "CONTROL 9: no file on the portable manifest compiles to an empty translation unit"
    else
        check 1 "CONTROL 9: no file on the portable manifest compiles to an empty translation unit ($vacuous did -- see the emits column; this is a manifest defect, not a broken rig)"
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

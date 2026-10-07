#!/usr/bin/env bash
#
# Proves the output guard holds in the SHIPPED BINARY, on a real document, on disk.
#
# tests/launch_options_tests.cpp asserts refuseOutput() returns the right enum for the right
# command line, and that is the whole decision -- but it is not the whole defect. The defect was
# that src/main_d2d.cpp collected every fact needed to refuse, printed one of them, and exported
# anyway. A unit test over the parser cannot fail on that, because the parser was never wrong.
# Only running the binary can, which is what this script is.
#
# IT RUNS qinta SINCE 2026-10-07. It was written against tinta.exe, the Direct2D app, which was
# deleted that day; qinta's src/main_qt.cpp carries the same guard (tinta::refuseOutput, the same
# messages), so every arm below asks qinta the question it asked tinta. --dumpchrome is no longer
# among the flags: it read DirectWrite's chrome formats back out and went with them.
#
# THE INCIDENT, verbatim, is arm 1. On 2026-09-20, from the repo root:
#
#     tinta --lite --exportpdf README.md
#
# replaced a tracked 2,357-line README.md with a 4,427,206-byte placeholder PDF, at EXIT 0, with
# `unknown option '--lite'` as the only line on stderr. It sat unnoticed for about three hours
# and the only detector in the tree turned out to be scripts/check-readme-figures.sh reporting
# eighteen misses.
#
# EVERYTHING HAPPENS IN A TEMP DIRECTORY, never in the repo, and that is not tidiness. A probe
# for a defect that destroys tracked files must not be able to destroy a tracked file while
# measuring whether it can. It cd's into the temp dir and passes RELATIVE paths, which also
# reproduces the incident's shape exactly -- a bare document name beside an output flag -- and
# sidesteps MSYS path translation on the way into a Windows binary.
#
# Run from anywhere:  scripts/probe-output-guard.sh
# Override the exe:   QINTA_EXE=/path/to/qinta.exe scripts/probe-output-guard.sh
# Qt's DLLs:          QT_BIN=/c/Qt/6.8.3/msvc2022_64/bin (the default), prepended to PATH
#
# Exit 0 only if every assertion below holds.

set -u -o pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TINTA_EXE="${QINTA_EXE:-$SOURCE_DIR/build-win-split/Release/qinta.exe}"
QT_BIN="${QT_BIN:-/c/Qt/6.8.3/msvc2022_64/bin}"

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

note 'Instrument'
# REFUSE BEFORE LAUNCHING, rather than launching and giving up. A qinta.exe that cannot start --
# a missing DLL, a half-written link -- puts a modal error box on the desktop and waits for a
# human to dismiss it, so a "run it and time out" shape costs an interruption rather than a
# reading. Measured on this tree once already with a Qt rig.
if [ ! -f "$TINTA_EXE" ]; then
    echo "FAIL : qinta.exe not found at $TINTA_EXE"
    echo "       Build it first, then re-run. Set QINTA_EXE to point elsewhere."
    exit 1
fi
# The same refusal for Qt itself: a qinta.exe with no Qt6Core.dll on PATH is the modal-box case.
if [ ! -f "$QT_BIN/Qt6Core.dll" ]; then
    echo "FAIL : Qt6Core.dll not found in $QT_BIN -- set QT_BIN to the kit's bin directory."
    exit 1
fi
PATH="$QT_BIN:$PATH"
# QT_HASH_SEED=0, or arm 5's masked comparison is a coin toss. qinta's PDF writer emits its fonts
# in QHash order, and Qt seeds QHash randomly per process, so a document with more than one font
# comes out with its font objects in a different order on each run: two exports of this script's
# fixture differed in their FontDescriptor objects after every date and id was masked, and three
# with the seed pinned were identical (2026-10-07). tinta never needed this; its fonts were DirectWrite's.
export QT_HASH_SEED=0
for tool in sha256sum mktemp; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "FAIL : $tool is required and was not found on PATH."
        exit 1
    fi
done
# RESOLVE PYTHON BY RUNNING IT, not by asking whether the name resolves. On this host `python3`
# resolves -- to the Microsoft Store's app-execution-alias shim, which prints an advert for the
# Store and exits without running anything. `command -v python3` therefore succeeds while every
# invocation fails, so the usual availability test is exactly the wrong test. Arm 5 needs a real
# interpreter for scripts/pdfmask.py; the first candidate that reports a version is it.
PYTHON=""
for candidate in ${TINTA_PYTHON:-} python python3 py; do
    if "$candidate" --version > /dev/null 2>&1; then PYTHON="$candidate"; break; fi
done
echo "binary : $TINTA_EXE"
echo "python : ${PYTHON:-NONE FOUND} $( [ -n "$PYTHON" ] && "$PYTHON" --version 2>&1 )"
echo "built  : $(date -r "$TINTA_EXE" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo unknown)"

WORK_DIR="$(mktemp -d -t tinta-guard-XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
echo "workdir: $WORK_DIR"
# The one thing about the work directory worth asserting: that it is NOT the repo. Every
# destructive arm below runs with the repo's own document names in a directory chosen by mktemp,
# so if that ever resolved somewhere inside the tree this script would be the defect.
case "$WORK_DIR" in
    "$SOURCE_DIR"*) check 1 "CONTROL: the work directory is OUTSIDE the repo (it is inside $SOURCE_DIR)" ;;
    *) check 0 "CONTROL: the work directory is outside the repo, so no arm below can touch a tracked file" ;;
esac

cd "$WORK_DIR" || exit 1

fixture() {
    printf '# Probe document\n\nSome **bold** prose, `code`, and a list:\n\n- one\n- two\n' > "$1"
}

# ---------------------------------------------------------------------------
note 'Arm 1: the exact command line that destroyed README.md'
fixture probe.md
before="$(sha256sum probe.md | awk '{print $1}')"
before_bytes="$(wc -c < probe.md | tr -d ' ')"
"$TINTA_EXE" --lite --exportpdf probe.md > arm1.out 2> arm1.err
arm1_rc=$?
after="$(sha256sum probe.md | awk '{print $1}')"
after_bytes="$(wc -c < probe.md | tr -d ' ')"
echo "exit       : $arm1_rc"
echo "bytes      : $before_bytes -> $after_bytes"
echo "sha256     : ${before:0:16} -> ${after:0:16}"
echo "stderr     : $(cat arm1.err)"

if [ "$before" = "$after" ]; then
    check 0 "the document is BYTE-IDENTICAL after the run -- the incident does not reproduce"
else
    check 1 "the document is byte-identical after the run (it changed: $before_bytes -> $after_bytes bytes, which is the 2026-09-20 defect still live)"
fi
# Exit code and hash are independent claims and both are needed. A binary that refused but
# exited 0 would leave every script calling it unable to tell, which is precisely how the
# incident stayed quiet for three hours -- the shell reported success.
if [ "$arm1_rc" != "0" ]; then
    check 0 "and the exit code is non-zero ($arm1_rc), so a caller can tell the write did not happen"
else
    check 1 "and the exit code is non-zero (it is 0 -- the refusal is invisible to any script)"
fi
# The message has to name the path and the reason. "It refused" is not a usable diagnostic when
# the user's next move is deciding which of two paths they typed backwards.
if grep -q "refusing to write 'probe.md'" arm1.err; then
    check 0 "and stderr names the path it refused to write"
else
    check 1 "and stderr names the path it refused to write (not found in arm1.err)"
fi
if grep -q 'names the file to WRITE, not the file to read' arm1.err; then
    check 0 "and says which way round the flag reads, which is the mistake being made"
else
    check 1 "and says which way round the flag reads (not found in arm1.err)"
fi
# No PDF may exist under any name. A guard that refuses the overwrite but still writes the
# export somewhere else has not refused, it has relocated.
pdfs="$(ls -1 *.pdf 2> /dev/null | wc -l | tr -d ' ')"
if [ "$pdfs" = "0" ]; then
    check 0 "and no PDF was produced at all, so the export did not merely land elsewhere"
else
    check 1 "and no PDF was produced at all ($pdfs found: $(ls -1 *.pdf | tr '\n' ' '))"
fi

# ---------------------------------------------------------------------------
note 'Arm 2: THE DISCRIMINATOR -- the same binary exporting the same document correctly'
# Without this arm, every assertion above is satisfied by a qinta.exe that cannot export at all.
# "The file did not change" and "the program is broken" produce identical readings, and this is
# the only thing here that separates them.
"$TINTA_EXE" --exportpdf out.pdf probe.md > arm2.out 2> arm2.err
arm2_rc=$?
echo "exit       : $arm2_rc"
echo "stderr     : $(cat arm2.err)"
if [ "$arm2_rc" = "0" ]; then
    check 0 "CONTROL: a correctly-pointed --exportpdf exits 0"
else
    check 1 "CONTROL: a correctly-pointed --exportpdf exits 0 (got $arm2_rc -- the arms above may be measuring a broken binary, not a guard)"
fi
if [ -f out.pdf ] && [ "$(head -c 4 out.pdf)" = "%PDF" ]; then
    check 0 "CONTROL: and writes a real PDF ($(wc -c < out.pdf | tr -d ' ') bytes, %PDF magic), so this binary CAN export"
else
    check 1 "CONTROL: and writes a real PDF (missing, or no %PDF magic -- so arm 1's unchanged document proves nothing)"
fi
unchanged="$(sha256sum probe.md | awk '{print $1}')"
if [ "$unchanged" = "$before" ]; then
    check 0 "CONTROL: and leaves the INPUT document untouched, which is the ordinary behaviour the guard must not disturb"
else
    check 1 "CONTROL: and leaves the input document untouched (it changed)"
fi

# ---------------------------------------------------------------------------
note 'Arm 3: a CLEAN command line pointed at its own input'
# No typo this time, so the CommandLineNotUnderstood test that caught arm 1 cannot fire. This is
# the refusal that would have caught the incident even if `--lite` had been spelled correctly,
# and it needs no filesystem question answered: the output path IS the input path.
"$TINTA_EXE" --exportpdf probe.md probe.md > arm3.out 2> arm3.err
arm3_rc=$?
arm3_after="$(sha256sum probe.md | awk '{print $1}')"
echo "exit       : $arm3_rc"
echo "stderr     : $(cat arm3.err)"
if [ "$arm3_after" = "$before" ] && [ "$arm3_rc" != "0" ]; then
    check 0 "a clean --exportpdf at its own input is refused, document byte-identical, exit $arm3_rc"
else
    check 1 "a clean --exportpdf at its own input is refused (exit $arm3_rc, hash $( [ "$arm3_after" = "$before" ] && echo unchanged || echo CHANGED))"
fi
if grep -q 'that is the document being opened' arm3.err; then
    check 0 "and the reason given is the RIGHT one -- same-as-input, not the typo test"
else
    check 1 "and the reason given is same-as-input (not found in arm3.err: $(cat arm3.err))"
fi

# ---------------------------------------------------------------------------
note 'Arm 4: a clean command line at an existing document with no input named'
# Neither earlier test can fire here -- the line has no typo and names no input -- so this is
# the extension-plus-existence test alone, which is the narrowest of the three and the one
# carrying the design decision this guard was reshaped around.
"$TINTA_EXE" --exportpdf probe.md > arm4.out 2> arm4.err
arm4_rc=$?
arm4_after="$(sha256sum probe.md | awk '{print $1}')"
echo "exit       : $arm4_rc"
echo "stderr     : $(cat arm4.err)"
if [ "$arm4_after" = "$before" ] && [ "$arm4_rc" != "0" ]; then
    check 0 "an export over an existing markdown document is refused with no input argument at all, exit $arm4_rc"
else
    check 1 "an export over an existing markdown document is refused (exit $arm4_rc, hash $( [ "$arm4_after" = "$before" ] && echo unchanged || echo CHANGED))"
fi
if grep -q 'a document already exists there' arm4.err; then
    check 0 "and the reason is the existing-document test rather than either earlier one"
else
    check 1 "and the reason is the existing-document test (not found in arm4.err: $(cat arm4.err))"
fi
# The same flag at the same kind of path that does NOT exist must be allowed, or the rule is
# "never write a .md" rather than "never destroy a document". One character of difference.
rm -f fresh.md
"$TINTA_EXE" --exportpdf fresh.md probe.md > arm4b.out 2> arm4b.err
arm4b_rc=$?
if [ "$arm4b_rc" = "0" ] && [ -f fresh.md ]; then
    check 0 "CONTROL: the same .md output path that does NOT exist is ALLOWED, so the test is existence and not extension alone"
else
    check 1 "CONTROL: a non-existent .md output path is allowed (exit $arm4b_rc, file $( [ -f fresh.md ] && echo written || echo MISSING) -- the guard is refusing on extension alone)"
fi

# ---------------------------------------------------------------------------
note 'Arm 5: THE FALSE-POSITIVE CONTROL -- the byte-identity rig shape, end to end'
# This is the arm that decides whether the guard survives contact with this tree. Every
# byte-identity harness re-exports to a FIXED scratch path on every run -- the invocation
# recorded in scripts/pdfmask.py's own header writes build-win-split/pdfscratch/m1.pdf every
# single time -- so a guard that refuses to overwrite an existing file breaks all of them on its
# first run and gets reverted with the defect's name still on it.
#
# out.pdf already exists from arm 2, so it must be overwritten without complaint, and the second
# export must match the first on the property the rigs actually measure.
#
# THAT PROPERTY IS NOT RAW BYTE-IDENTITY, and asserting raw identity here FAILED on the first run
# -- correctly. The Windows arm's PDF carries exactly two `(D:yyyymmddhhmmss` strings,
# CreationDate and ModDate, so two runs of the same build over the same input differ in those
# fourteen-digit spans and nowhere else. scripts/pdfmask.py exists for that reason and is what
# every PDF baseline in this tree is recorded through. The comparison therefore runs through THAT
# script rather than a second implementation of its rule: the masked digest is the number the
# rigs are keyed on, which makes it the only digest whose stability is worth asserting, and an
# instrument that agreed with a freshly-written copy of itself would prove nothing.
first_pdf="$(sha256sum out.pdf | awk '{print $1}')"
mask() {
    if [ -z "$PYTHON" ]; then
        echo "no python interpreter was resolved"
        return
    fi
    "$PYTHON" "$SOURCE_DIR/scripts/pdfmask.py" "$1" 2>&1
}
first_mask="$(mask out.pdf)"
"$TINTA_EXE" --exportpdf out.pdf probe.md > arm5.out 2> arm5.err
arm5_rc=$?
second_pdf="$(sha256sum out.pdf | awk '{print $1}')"
second_mask="$(mask out.pdf)"
first_digest="$(printf '%s\n' "$first_mask" | sed -n 's/.*sha256=//p')"
second_digest="$(printf '%s\n' "$second_mask" | sed -n 's/.*sha256=//p')"
first_dates="$(printf '%s\n' "$first_mask" | sed -nE 's/.*dates_masked=([0-9]+).*/\1/p')"
echo "exit       : $arm5_rc"
echo "raw sha256 : ${first_pdf:0:16} -> ${second_pdf:0:16} (expected to DIFFER: two date strings)"
echo "masked     : ${first_digest:0:16} -> ${second_digest:0:16}"
echo "dates      : masked=${first_dates:-none}"
if [ "$arm5_rc" = "0" ]; then
    check 0 "CONTROL: re-exporting over an EXISTING .pdf is allowed -- the rigs keep working"
else
    check 1 "CONTROL: re-exporting over an existing .pdf is allowed (exit $arm5_rc -- this guard breaks every byte-identity harness in the tree)"
fi
# A SILENT MASKER IS NOT A MASK. If python3 were missing, or the date pattern matched nothing,
# both digests would come back empty, compare equal, and this arm would pass having compared
# nothing at all -- which is the exact shape pdfmask.py's own dates_masked counter was added to
# prevent. So assert the count first, and assert it is 2, the number that script's header records.
if [ "${first_dates:-0}" = "2" ]; then
    check 0 "CONTROL: pdfmask.py masked 2 date spans, so the digest below is a real normalisation"
else
    check 1 "CONTROL: pdfmask.py masked 2 date spans (reported '${first_dates:-nothing}' -- is python3 missing? output: $first_mask)"
fi
if [ -n "$first_digest" ] && [ "$first_digest" = "$second_digest" ]; then
    check 0 "CONTROL: and the two exports are identical once the dates are masked, so the guard did not perturb what the rigs measure"
else
    check 1 "CONTROL: and the two exports are identical once the dates are masked (they differ -- unrelated to this guard, but every PDF baseline in the tree will read it as a regression)"
fi
# And the RAW bytes must differ, which is the other half of the same claim and the direction
# nobody checks. If they ever stopped differing, the mask would be pure ceremony and the check
# above would be a tautology -- so the non-determinism is asserted rather than assumed.
if [ "$first_pdf" != "$second_pdf" ]; then
    check 0 "CONTROL: and the RAW bytes do differ, so the mask above is load-bearing and not ceremony"
else
    check 1 "CONTROL: and the raw bytes do differ (they are identical -- the PDF has become deterministic, so pdfmask.py's premise needs re-reading)"
fi

# The other two destructive flags at existing non-document targets, because the guard is
# written once and applies to all of them. Naming only --exportpdf would leave two flags whose
# behaviour is asserted by the unit test's enum and by nothing that runs.
for pair in "--exporthtml out.html" "--exportdocx out.docx"; do
    flag="${pair% *}"
    path="${pair#* }"
    : > "$path"
    "$TINTA_EXE" "$flag" "$path" probe.md > "arm5-$flag.out" 2> "arm5-$flag.err"
    rc=$?
    size="$(wc -c < "$path" | tr -d ' ')"
    if [ "$rc" = "0" ] && [ "$size" -gt 0 ]; then
        check 0 "CONTROL: $flag over an existing $path is allowed and wrote $size bytes"
    else
        check 1 "CONTROL: $flag over an existing $path is allowed (exit $rc, $size bytes -- see arm5-$flag.err: $(cat "arm5-$flag.err"))"
    fi
done

# And the document test applies to every one of them, not just the PDF exporter. Same loop,
# output pointed at the document: all three must refuse and probe.md must survive all three.
refused=0
for flag in --exportpdf --exporthtml --exportdocx; do
    "$TINTA_EXE" "$flag" probe.md > /dev/null 2> "arm6-$flag.err"
    rc=$?
    if [ "$rc" != "0" ] && grep -q 'refusing to write' "arm6-$flag.err"; then
        refused=$((refused + 1))
    else
        printf '       %s did NOT refuse (exit %s): %s\n' "$flag" "$rc" "$(cat "arm6-$flag.err")"
    fi
done
final="$(sha256sum probe.md | awk '{print $1}')"
if [ "$refused" = "3" ]; then
    check 0 "all THREE file-writing exporters refuse an existing document, not just the one the incident used"
else
    check 1 "all three file-writing exporters refuse an existing document (only $refused did)"
fi
if [ "$final" = "$before" ]; then
    check 0 "and probe.md is byte-identical after every arm in this script"
else
    check 1 "and probe.md is byte-identical after every arm (it changed somewhere above)"
fi

note 'Result'
if [ "$failures" = "0" ]; then
    echo "PASS: the output guard holds in the shipped binary, and the export path still works."
    exit 0
fi
echo "$failures assertion(s) failed."
exit 1

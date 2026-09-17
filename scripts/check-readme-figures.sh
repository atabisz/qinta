#!/usr/bin/env bash
#
# Re-derives every countable figure the README quotes about the port and asserts
# that the README actually says it. Run it from the repo root, on any platform
# with bash; it reads the source tree only and needs no build.
#
# WHY THIS EXISTS, and the answer is a measurement rather than a preference. The
# completeness section of README.md quotes roughly forty derived numbers -- file
# counts, line counts, error counts, native-mention counts, percentages. The pass
# that added the FILETIME seam re-derived all of them by hand and found FIVE stale:
#
#   * render.cpp's line count read 3,385 for two passes; it was 3,301.
#   * The "To do" total read 29,903; re-derived at that same commit it is 29,959.
#     Stale by 56 lines, in the reassuring direction.
#   * text_d2d.cpp read 635 after gaining 132 lines.
#   * platform_paths_win32.cpp read 146 after gaining 27.
#   * d2d_init.cpp read 681; it was 683.
#
# Every one was correct when written. That is the hard kind of stale: nothing about
# the sentence looks wrong, the pass that broke it did not touch it, and there is no
# neighbouring figure for it to disagree with. The section had even congratulated
# itself on the "total" being reliable, in the same paragraph that fixed three
# rotted rows -- and the total rotted the very next pass.
#
# So this is not belt-and-braces over some other instrument. For these figures it is
# the ONLY instrument that exists: the probe checks the manifest, ctest checks the
# tests, g++ checks the code, and NOTHING checks prose.
#
# WHAT IT MATCHES, and this is the part the control rewrote. The first version of
# this script looked for each derived NUMBER anywhere in the README. Its positive
# control -- look for n+1 instead, expect every check to fail -- found 6 of 39 still
# matching, all of them small integers between 22 and 38 that collide with some
# other count in a document made of counts. That is precisely the "read the column,
# not the digit" trap the README itself warns about, walked into by the checker
# meant to enforce it. So every assertion here is a whole CLAIM STRING built from
# derived values -- a table row, an additive expression, a sentence -- and a figure
# is only "found" when the sentence around it is found too. That makes the script
# sensitive to editorial rewording, which is the correct trade: a reworded claim
# should be re-checked, and a silently rotted number should not pass.
#
# Exit status is the reading: 0 means every derived claim was found, non-zero is the
# number that were not. The failure count prints BEFORE any summary line, so a
# truncated log cannot show a pass that did not happen.
#
# POSITIVE CONTROL: `--control` rebuilds every claim with each derived component
# incremented by one. All of them must then miss. A run of this script that has not
# been controlled proves only that grep found something.

set -u

README=README.md
control=0
[ "${1:-}" = "--control" ] && control=1

if [ ! -f "$README" ]; then
    echo "FATAL: run from the repo root -- $README not found"
    exit 90
fi

failures=0
checks=0

# In control mode every derived number is bumped by one AS IT IS COMPUTED, so the
# claim strings are assembled wrong end to end rather than being string-edited
# afterwards -- a substitution on the finished sentence could miss a component and
# leave the control weaker than it looks.
bump() { if [ "$control" -eq 1 ]; then echo $(($1 + 1)); else echo "$1"; fi; }

lines() { echo "$(bump "$(wc -l < "$1" | tr -d ' ')")"; }

# 3307 -> 3,307
c() { printf '%s' "$1" | sed ':a;s/\B[0-9]\{3\}\>/,&/;ta'; }

# Asserts a literal claim string appears in the README exactly once. Once, not
# at-least-once: a figure appearing twice is how the same claim ends up corrected in
# one place and left standing in the other, which this port has now done twice.
claim() {
    local label="$1" text="$2" n
    checks=$((checks + 1))
    n=$(grep -cF "$text" "$README" || true)
    if [ "$n" -eq 1 ]; then
        printf '  ok   %-42s %s\n' "$label" "$text"
    elif [ "$n" -eq 0 ]; then
        printf '  MISS %-42s %s\n' "$label" "$text"
        failures=$((failures + 1))
    else
        printf '  DUP  %-42s %s   (appears %s times)\n' "$label" "$text" "$n"
        failures=$((failures + 1))
    fi
}

pct() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.1f", (a*100.0)/b}'; }

# --- the manifest and the two sets derived from it --------------------------
manifest=$(grep -vE '^[[:space:]]*(#|$)' tests/portable-sources.txt | sort)
manifest_count=$(bump "$(printf '%s\n' "$manifest" | grep -c . || true)")
all_cpp=$(ls src/*.cpp | xargs -n1 basename | sort)
all_cpp_count=$(bump "$(printf '%s\n' "$all_cpp" | grep -c . || true)")
todo=$(comm -23 <(printf '%s\n' "$all_cpp") <(printf '%s\n' "$manifest"))
todo_count=$(bump "$(printf '%s\n' "$todo" | grep -c . || true)")

raw_todo_lines=0
for f in $todo; do raw_todo_lines=$((raw_todo_lines + $(wc -l < "src/$f"))); done
todo_lines=$(bump "$raw_todo_lines")

raw_numerator=0
for f in $manifest; do raw_numerator=$((raw_numerator + $(wc -l < "src/$f"))); done
numerator=$(bump "$raw_numerator")

raw_src_lines=0
for f in src/*.cpp; do raw_src_lines=$((raw_src_lines + $(wc -l < "$f"))); done
raw_denominator=$raw_src_lines
for f in src/*.h include/*.h; do
    [ -f "$f" ] && raw_denominator=$((raw_denominator + $(wc -l < "$f")))
done
src_cpp_lines=$(bump "$raw_src_lines")
denominator=$(bump "$raw_denominator")

echo "=== derived from the tree, then looked for in $README ==="
[ "$control" -eq 1 ] && echo "    (CONTROL RUN: every component is bumped by one; all $((0)) checks must MISS)"
echo

echo "-- the bases table"
claim "basis A"          "| $(c "$numerator") / $(c "$denominator") | **$(pct "$numerator" "$denominator")%** |"
claim "basis B"          "| $manifest_count / $all_cpp_count | $(pct "$manifest_count" "$all_cpp_count")% |"
claim "basis A ceiling"  "$(c "$src_cpp_lines") lines of the $(c "$denominator")"

# The ceiling percentage needs a BIGGER perturbation than every other check, and the
# reason is a property of the figure rather than a weakness in the claim. It is
# rounded to one decimal, so a +1 on both its numerator and denominator leaves it
# reading 87.3% either way -- the control passed it, correctly, and a control that
# cannot fire is not a control. So in control mode the numerator moves by 0.2
# percentage points' worth of lines instead, which must move the first decimal.
# The un-perturbed value is computed from the raw counts, never from the bumped
# ones, so the real check is unaffected by any of this.
ceiling_num=$raw_src_lines
if [ "$control" -eq 1 ]; then
    ceiling_num=$((raw_src_lines + (raw_denominator / 500)))
fi
claim "basis A ceiling %" "**basis A's ceiling is $(pct "$ceiling_num" "$raw_denominator")%, not 100%**"

echo "-- the to-do set"
claim "to-do header"     "**To do — $todo_count files, $(c "$todo_lines") lines**"

echo "-- to-do table rows, as the additive expressions they are written in"
# Grouped exactly as the table groups them, so one rotted component fails the row it
# lives in and names its neighbours in the output.
claim "window + message loop" "| $(c "$(lines src/input.cpp)") + $(c "$(lines src/main_d2d.cpp)") |"
claim "editor"                "| Editor | $(c "$(lines src/editor.cpp)") |"
claim "annotations"           "| Annotations | $(c "$(lines src/annotations.cpp)") |"
# math_render.cpp is now DELIBERATELY absent from this row, and it is the THIRD file to
# exercise the rule the export_html.cpp note below states -- it compiles, links and RUNS off
# Windows, so it left tests/portable-sources.txt's to-do side and therefore left here. Note
# what that does to the row's SHAPE and not just its number: this was the "Annotations, math"
# row and it is now "Annotations" alone, so the label was a claim too. A row that keeps a
# two-term additive expression after one term leaves does not read as stale -- it reads as a
# considered pairing, which is the failure mode the note below warns about, in the direction
# it warns about.
# overlays.cpp is now DELIBERATELY absent too, by the same rule the export_html.cpp note
# below states -- it reached 0 errors and joined the manifest, so it left the to-do table
# and therefore left this row. It is the second file to exercise that rule, and the first
# to exercise it in the direction the note warns about: the row kept asserting overlays'
# line count for exactly as long as it took to run this gate, and the gate is the only
# thing that noticed. Had the row been hand-written rather than derived, the stale figure
# would have read as a current measurement.
# export_html.cpp is DELIBERATELY absent from this row: it left the to-do set when it
# reached 0 errors and joined tests/portable-sources.txt, so deriving the row from it
# would keep asserting a README figure that must no longer include it. This row is
# hand-maintained against the to-do TABLE, which the manifest does not drive -- so it
# is the one row that can rot silently in the *other* direction, by keeping a file the
# tree has since freed. When a file leaves the to-do set, it leaves here too.
claim "exports and printing"  "| $(c "$(lines src/export_docx.cpp)") + $(c "$(lines src/print.cpp)") + $(c "$(lines src/pandoc.cpp)") |"
claim "shell integration"     "| $(c "$(lines src/tabs.cpp)") + $(c "$(lines src/signals.cpp)") + $(c "$(lines src/drafts.cpp)") |"
claim "clipboard, misc"       "| Clipboard, misc | $(c "$(lines src/utils.cpp)") |"
claim "startpage"             "| Startpage | $(c "$(lines src/startpage.cpp)") |"
claim "device and factory"    "| Device and factory creation | $(c "$(lines src/d2d_init.cpp)") |"

backends="render_d2d text_d2d settings_win32 platform_paths_win32 settings_app keyboard_win32 image_win32"
expr_backends=""
raw_backend_lines=0
for f in $backends; do
    n=$(lines "src/$f.cpp")
    raw_backend_lines=$((raw_backend_lines + n))
    expr_backends="${expr_backends:+$expr_backends + }$(c "$n")"
done
nbackends=$(printf '%s\n' $backends | grep -c . || true)
claim "the seven Windows backends"   "| $expr_backends |"
claim "  ... and their total"        "$nbackends files, $(c "$raw_backend_lines") lines"
claim "  ... quoted again in prose"  "$(c "$raw_backend_lines") lines of it today"

# LABEL CORRECTED IN PLACE, TWICE NOW, AND THE SECOND TIME IS THE INTERESTING ONE. It first
# said "render.cpp, the blocker"; the file started compiling off Windows and the label was
# then asserting something false on every run, so it became "now a link blocker rather than a
# compile blocker" with a comment here explaining that qinta was six math symbols short.
# On 2026-09-16 it stopped being a blocker of either kind: src/math_stub.cpp closed the six
# symbols, QINTA_APP_SOURCES closed the nineteen files, and qt_entry_tests links and RUNS
# this file on both platforms. A label is a claim like any other figure -- just one no checker
# checks, which is exactly why it has now been wrong twice and caught twice by hand.
echo "-- render.cpp, no longer a blocker of either kind (compiles, links and runs off Windows)"
claim "render.cpp row" "| \`render.cpp\` | $(c "$(lines src/render.cpp)") |"

# Native mentions, under the pattern the README quotes beside the figures. If the
# pattern changes there it must change here, and the two disagreeing is the finding.
pattern='\b(ID2D1|IDWrite|IWIC|D2D1_|DWRITE_|WIC[A-Z])[A-Za-z0-9_]*|D2D1::'
if command -v rg >/dev/null 2>&1; then
    mentions=$(bump "$(rg -o --pcre2 "$pattern" src/render.cpp | grep -c . || true)")
    live=$(bump "$(grep -vE '^[[:space:]]*//' src/render.cpp | rg -o --pcre2 "$pattern" | grep -c . || true)")
    claim "mention-trajectory table, last row" \
          "| current tree | $(c "$(lines src/render.cpp)") | $mentions | $live |"
else
    echo "  skip render.cpp mention counts -- rg not on PATH (the pattern needs PCRE2)"
fi

# The README quotes 21 CALLS and notes a grep returns 22, the extra being a comment
# in render.cpp:90 recording the old census. Both are asserted, because the entire
# point of that sentence is the gap between them.
grep_total=$(grep -rn 'dwriteFactory->CreateTextLayout' src/ | grep -c . || true)
comment_hits=$(grep -rn 'dwriteFactory->CreateTextLayout' src/ | grep -cE ':[[:space:]]*//' || true)
claim "direct CreateTextLayout calls" \
      "**$(bump "$((grep_total - comment_hits))") direct \`dwriteFactory->CreateTextLayout\` calls remain in the tree**"
claim "  ... vs what a bare grep returns" \
      "\`grep\` for the call returns $(bump "$grep_total")"

# --- the reading, failures FIRST -------------------------------------------
echo
echo "readme_figure_failures=$failures"
echo "readme_figures_checked=$checks"
if [ "$control" -eq 1 ]; then
    if [ "$failures" -eq "$checks" ]; then
        echo "CONTROL PASSED: all $checks claims missed when perturbed, so each is anchored to its own sentence"
        exit 0
    fi
    echo "CONTROL FAILED: $((checks - failures)) perturbed claim(s) still matched -- those matches are too loose to trust"
    exit 1
fi
[ "$failures" -eq 0 ] && echo "All $checks derived claims are the ones the README states."
exit "$failures"

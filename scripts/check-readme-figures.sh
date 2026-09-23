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
# tests, g++ checks the code, and nothing else checks prose.
#
# AS OF 2026-09-19 CTEST RUNS THIS SCRIPT, as the `readme_figures` and
# `readme_figures_control` arms (CMakeLists.txt, outside the WIN32 block). So "ctest
# checks the tests" and "nothing checks prose" are no longer two separate facts, and the
# sentence above is corrected rather than deleted because the reason it was written still
# holds: nothing else in the repo reads prose, and this file is still the only thing that
# does. What changed is who types it.
#
# ONE THING THE WIRING ADDED THAT MATTERS TO ANYONE EDITING THIS FILE. The arms assert
# the printed readme_figures_checked= count as well as the exit status, because of the
# `command -v rg` guard further down: without ripgrep this script checks SIXTEEN claims,
# finds nothing wrong, and exits 0 saying so. Measured, not feared -- run with rg off PATH
# it prints "All 16 derived claims are the ones the README states." So if you add a claim
# here, move EXPECTED_CHECKS in CMakeLists.txt with it; the driver fails loudly in both
# directions and says which direction it was.
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

# A SECOND, SEPARATE PAIR OF COUNTERS, and the separation is load-bearing. They are named
# `platform_*` and not `manifest_*` because `manifest`/`manifest_count` below already mean
# tests/portable-sources.txt, and two different manifests under one prefix is how the wrong
# authority gets cited. `checks`/`failures`
# count claims about what README.md SAYS, and the control arm's whole test is that they are equal.
# The tests/test-platforms.txt fidelity checks added in 2026-09-21 are claims about what the BUILD
# does, they are not perturbable by --control, and folding them into `failures` would make a
# passing control arithmetically impossible. So they get their own counters, their own printed
# line, and their own assertion in tests/run_figure_gate.cmake -- read the line, not the exit code.
platform_failures=0
platform_skips=0

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
# The count is DELIBERATELY absent from this banner. It used to read `all $((0)) checks must
# MISS`, printed before a single claim had run, so it always said zero and then ran eighteen --
# a control arm whose own banner understated it by the whole suite. The honest count is the
# `readme_figures_checked=` line at the end, which is printed after the claims rather than
# before them, and `readme_figure_failures=` must EQUAL it on a control run.
[ "$control" -eq 1 ] && echo "    (CONTROL RUN: every component is bumped by one; every check below must MISS,"
[ "$control" -eq 1 ] && echo "     so a passing control is readme_figure_failures == readme_figures_checked)"
echo

echo "-- the bases table"
claim "basis A"          "| $(c "$numerator") / $(c "$denominator") | **$(pct "$numerator" "$denominator")%** |"
claim "basis B"          "| $manifest_count / $all_cpp_count | $(pct "$manifest_count" "$all_cpp_count")% |"

# BASIS C's DENOMINATOR, GATED 2026-09-19, AND IT NEEDED NO NEW MEASUREMENT AT ALL: it is
# $all_cpp_count, the SAME variable basis B's denominator comes from. C counts which of
# src/*.cpp a running test links off Windows; B counts which of src/*.cpp compile without
# Windows headers. Different numerators over ONE set.
#
# THE README SPENT A PARAGRAPH EXPLAINING WHY THIS COULD NOT BE DONE, AND THE EXPLANATION WAS
# INVENTED. It read: "C's denominator was 73 while B's was 74, and nothing here records which
# single src/*.cpp file the exclusion is" -- and concluded the figure "cannot be derived from
# this document". There is no exclusion. Every commit that ever edited the C row published the
# two denominators EQUAL: cea6c6b 69/69, cf385ce 70/70, f8e8973 71/71, a7bdb91 70/70,
# effd2de 72/72, 0a29eb5 73/73. B then moved to 74 (3645c8d) and 75 (54740ac) and the C row was
# left behind for 17 commits. A gap of one was read as an exclusion, the exclusion could not be
# found, and its absence became the reason not to gate -- so a wrong EXPLANATION cost more than a
# wrong figure ever does, because a figure gets caught by a gate and this removed the gate from
# consideration.
#
# The near-miss is worth a line because it reproduces today's number exactly: src/ holds two real
# entry points (main_d2d.cpp's WinMain, main_qt.cpp's main), 75 - 2 = 73, and "a test binary has
# its own main, so an entry point can never be on a test's link line" is a TRUE rule that is not
# this rule. It contradicts all six historical passes. That rule is real and it lands in the
# NUMERATOR, where main_qt.cpp duly fails to appear.
#
# WHAT THIS CLAIM CANNOT DO, stated because the gate has been over-read before: the numerator is
# parsed OUT OF the row and put straight back, so this cannot catch a wrong numerator -- only a
# stale denominator, or a percentage inconsistent with the pair. The numerator is a link-time
# fact that no static script can see; its instrument is scripts/probe-portable-linux.sh, which
# has the link lines. Deliberately NOT bumped in control mode for the same reason: the control
# must fire on the component this script DERIVES, and a control that also perturbs the part read
# from the document would pass without ever testing the derivation.
c_row=$(sed -nE 's/^\| C\. Files a running test links \*\*off Windows\*\* \| ([0-9]+) \/ [0-9]+ \|.*/\1/p' "$README" | head -1)
[ -n "$c_row" ] || c_row=0
claim "basis C denominator (= B's)" \
      "| C. Files a running test links **off Windows** | $c_row / $all_cpp_count | $(pct "$c_row" "$all_cpp_count")% |"
# BASES D AND E, GATED 2026-09-21, AND THEY ARE THE TWO ROWS THIS SCRIPT USED TO LEAVE ALONE.
# They read `30 / 35` and `30 / 39` for several passes while the suite grew from 39 test targets
# to 50 -- stale inside the gated table, one line below three figures this script drags to the
# truth on every commit. A stale row in a gated table is worse than a stale paragraph, because
# the table is the part a reader trusts.
#
# THE REASON THEY WERE UNGATED WAS REAL AND IS NOW ANSWERED RATHER THAN ARGUED AWAY. D and E are
# claims about TWO HOSTS, and this script runs on one at a time, so neither row is derivable from
# the tree the way A, B and C are. The answer is tests/test-platforms.txt: one checked-in row per
# ctest test name carrying its platform reach, from which D and E are pure arithmetic available on
# any host -- and whose own truth is asserted from BOTH ends, the Windows ctest list here and a
# fresh non-Windows configure in scripts/probe-portable-linux.sh.
#
# WHAT HAND-DERIVING THEM COST, because it is the reason the manifest's reason column is gated
# instead of documented. Deriving D by hand produced a false finding that reached pushed history:
# seven targets described as "linking only portable sources and running on Windows only for want
# of a CMakeLists.txt entry" -- cheap wins. Five of them compile the whole Windows stack, reaching
# it through ${INPUT_TEST_SOURCES} rather than through a filename the derivation was matching on,
# and one is Windows-only by subject, which the comment directly above it in CMakeLists.txt
# already said in those words. Zero were cheap. So `sources:` and `subject:` are not labels here;
# each is an assertion with a probe, checked in both directions below.
#
# D's DENOMINATOR DELIBERATELY COUNTS BY SOURCES ONLY. Counting "could move" by subject as well
# would make the row 38 / 38, a tautology that cannot go red. By sources, config_paths joins the
# denominator -- every file it compiles is portable and it stays on Windows because <windows.h> is
# in the test itself -- and the row then states something worth publishing.
PLATFORMS=tests/test-platforms.txt
platform_rows=$(grep -vE '^[[:space:]]*(#|$)' "$PLATFORMS" || true)
prow() { printf '%s\n' "$platform_rows" | awk -v p="$1" '$2==p' ; }
pcount() { prow "$1" | grep -c . || true; }
both_count=$(pcount both)
winonly_count=$(pcount windows-only)
# The `subject:` rows are the ones whose SOURCES are entirely portable, so they are the only
# Windows-only targets that belong in D's denominator.
subject_winonly=$(prow windows-only | awk '$3=="subject:"' | grep -c . || true)

d_num=$(bump "$both_count")
d_den=$(bump "$((both_count + subject_winonly))")
e_num=$(bump "$both_count")
e_den=$(bump "$((both_count + winonly_count))")
claim "basis D" \
      "| D. Test targets that *could* move, and do | $d_num / $d_den | $(pct "$d_num" "$d_den")% |"
claim "basis E" \
      "| E. All test targets, run off Windows | $e_num / $e_den | $(pct "$e_num" "$e_den")% |"

# THE DECOMPOSITION UNDER THOSE TWO TOTALS IS GATED TOO, and that is the point of this loop rather
# than a flourish. This document's standing failure mode is a gated TOTAL sitting above an ungated
# breakdown, and the passage D's new figure replaced was exactly that: seven targets published as
# cheap wins with their blocker counts never measured. The README now carries a per-target blocker
# table, and publishing that ungated would repeat the same error one row lower down the page.
#
# THE CHAIN HAS NO UNGATED LINK AND IT IS DELIBERATELY TWO HOPS, because one hop cannot be checked
# on both hosts:
#   the README table <- claimed here against tests/test-platforms.txt's reason column, a checked-in
#                       file and therefore readable on ANY host;
#   the manifest     <- checked against build-win-split/*.vcxproj by the fidelity block far below,
#                       which SKIPS off Windows because there is no project file to census there.
# Collapsing the two would make these twelve claims skip off Windows as well, and then
# EXPECTED_CHECKS would have to differ per host -- which is how a second reading stops being one.
#
# The claimed literal is a PREFIX of the table row (`| name | N |`), so the third column's prose can
# change freely without touching this; `claim` still requires it exactly once in the file.
while read -r bname _bplat bkind brest; do
    [ -n "$bname" ] || continue
    case "$bkind" in
        sources:) bn=$(printf '%s' "$brest" | awk '{print $1}') ;;
        subject:) bn=0 ;;
        *)        bn="" ;;
    esac
    # A non-numeric count claims a literal no table row can carry, so a malformed reason MISSes here
    # as well as failing the well-formedness check below. It must NOT be coerced to a number: 0 is a
    # meaningful value in this column -- it is what makes `config_paths` a subject row -- so a silent
    # 0 would assert the exact opposite of what a broken row says.
    case "$bn" in ''|*[!0-9]*) bn=999999 ;; esac
    claim "blockers: $bname" "| \`$bname\` | $(bump "$bn") |"
done <<EOF
$(prow windows-only | sort)
EOF

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

# --- the non-manifest census paragraph -------------------------------------
# WHY THIS BLOCK EXISTS. That paragraph published `clean: 1  fails: 17` out of "a census of 18"
# while the tree held 19 non-manifest files reading 1 clean, 2 vacuous and 16 failing. It had
# drifted twice -- 16 of 17, then 17 of 18 -- and nothing went red, because nothing here asserted
# anything about it. Worse, the README claimed IN PROSE that this script derived those counts, so
# the one reader who might have checked was told not to bother.
#
# WHAT THIS CAN AND CANNOT DO, which is the whole design of the block. This script cannot run g++,
# so it can never know whether `fails: 16` is true. What it CAN own is the denominator -- src/*.cpp
# minus the manifest, which is exactly $todo and is derived from the tree on every run -- and the
# arithmetic of the published split against it. So the three verdict counts are read back OUT of
# the README and checked for SUM against a number that came from the tree. That is a real check and
# not a mirror: README cannot satisfy it by being internally consistent, because the total it has
# to hit is not in the README.
#
# What it therefore CANNOT catch is a split that is internally consistent, sums correctly, and is
# wholly stale against the compiler -- 3 + 0 + 16 = 19 would pass this gate and be false. The
# README says so where the figure is published, rather than only here, because the person about to
# trust the number is reading the README.
claim "census denominator"  "that fail out of a census of **$todo_count**"

# A parse that finds nothing must FAIL rather than become three zeros. Against a bumped
# denominator three zeros would sum wrong and look like a working check, but against the real one
# a 0-row parse of a 0-file census would sum RIGHT -- so the parse is asserted first and separately
# from its arithmetic.
#
# AND THE FIRST VERSION OF THIS BLOCK SELECTED ON SHAPE ALONE, which was wrong and went red on its
# first real run. `clean: N  vacuous: N  fails: N` matched TWICE: once at the published figure, and
# once at the counterexample two paragraphs down that exists to show this gate's blind spot. So the
# selector is now POSITIVE -- the figure is the one on the line carrying $CENSUS_ANCHOR -- and the
# lesson generalises past this block: a gate that identifies its subject by format cannot tell a
# measurement from an illustration OF a measurement, and prose about a number is exactly where the
# second one lives. Do not fix a collision like that by reformatting the prose; the counterexample
# has to keep the shape of the thing it is a counterexample to, or it stops being one.
#
# Two guards around the arithmetic, and neither can pass silently:
#   * exactly one line may carry the anchor. Zero means the anchor phrase was reworded and this
#     check has gone blind -- which MISSes, rather than falling through to a different match.
#   * the total population of shape matches must be $CENSUS_SHAPES. A third census figure
#     appearing anywhere in the README is then loud even though this block does not check it.
# Both are folded into the SAME check as the arithmetic rather than counted separately, because
# --control only bumps the denominator: a check that does not read $todo_count cannot fail in the
# control arm, and the control arm's own criterion is failures == checked.
CENSUS_ANCHOR='excludes the empty ones:**'
CENSUS_SHAPES=2
checks=$((checks + 1))
census_all=$(grep -cE 'clean: [0-9]+ +vacuous: [0-9]+ +fails: [0-9]+' "$README" || true)
census_line=$(grep -F -- "$CENSUS_ANCHOR" "$README" \
    | grep -oE 'clean: [0-9]+ +vacuous: [0-9]+ +fails: [0-9]+' || true)
census_hits=$(printf '%s\n' "$census_line" | grep -c . || true)
if [ "$census_hits" -ne 1 ]; then
    printf '  MISS %-42s %s\n' "census decomposition" \
        "expected one 'clean: N  vacuous: N  fails: N' on the line carrying '$CENSUS_ANCHOR' in $README, found $census_hits"
    failures=$((failures + 1))
elif [ "$census_all" -ne "$CENSUS_SHAPES" ]; then
    printf '  MISS %-42s %s\n' "census decomposition population" \
        "$README holds $census_all census figures, expected $CENSUS_SHAPES (the measurement plus one counterexample); if you added or removed one deliberately, move CENSUS_SHAPES with it"
    failures=$((failures + 1))
else
    c_clean=$(printf '%s' "$census_line" | sed -E 's/.*clean: ([0-9]+).*/\1/')
    c_vac=$(printf   '%s' "$census_line" | sed -E 's/.*vacuous: ([0-9]+).*/\1/')
    c_fail=$(printf  '%s' "$census_line" | sed -E 's/.*fails: ([0-9]+).*/\1/')
    c_sum=$((c_clean + c_vac + c_fail))
    if [ "$c_sum" -eq "$todo_count" ]; then
        printf '  ok   %-42s %s\n' "census decomposition sums" \
            "$c_clean + $c_vac + $c_fail = $c_sum, the $todo_count non-manifest src/*.cpp this script derives"
    else
        printf '  MISS %-42s %s\n' "census decomposition sums" \
            "README publishes $c_clean + $c_vac + $c_fail = $c_sum, but the tree has $todo_count non-manifest src/*.cpp"
        failures=$((failures + 1))
    fi
fi

echo "-- to-do table rows, as the additive expressions they are written in"
# Grouped exactly as the table groups them, so one rotted component fails the row it
# lives in and names its neighbours in the output.
#
# THIS ROW'S MEMBERSHIP IS DERIVED FROM THE TO-DO SET AS OF 2026-09-21, AND IT IS THE ONLY ROW
# HERE THAT EVER NEEDED TO BE, because it is the only one whose hand-written pair could lose a
# member while the other stayed. src/input.cpp compiles off Windows at 0 errors and is in
# tests/portable-sources.txt's compile-only section, so by the rule the export_html.cpp note
# below states it leaves this gate too -- and at 4,586 lines it is the LARGEST departure this
# gate has processed, ahead of editor.cpp's 3,207 and overlays.cpp's 4,153.
#
# WHAT SETTLED IT WAS THE ARITHMETIC RATHER THAN THE RULE, which is worth recording because the
# rule alone would have been a judgment call: the to-do header above derives 9,194 lines, and
# the rows of that table sum to exactly 9,194 ONLY with input.cpp out -- 2,061 + 1,234 + 1,615
# + 516 + 3,768. With it in they sum to 13,780. Keeping the hand-written pair would have made
# two GATED figures on the same page contradict each other by 4,586 lines, which is worse than
# either one being stale, because both would have read as checked.
#
# Deriving the membership rather than editing the pair is the fix for the class and not the
# instance: the next departure moves this row on its own. An empty set is a loud FAIL and never
# a silent pass -- when main_d2d.cpp follows, this block gets deleted the way every other
# departure's did, and the gate says so rather than asserting an empty expression.
wml_files=$(printf '%s\n' $todo | grep -xE 'input\.cpp|main_d2d\.cpp' || true)
expr_wml=""
for f in $wml_files; do
    if [ -n "$expr_wml" ]; then expr_wml="$expr_wml + "; fi
    expr_wml="$expr_wml$(c "$(lines "src/$f")")"
done
if [ -n "$expr_wml" ]; then
    claim "window + message loop" "| $expr_wml |"
else
    checks=$((checks + 1))
    printf '  FAIL %-42s %s\n' "window + message loop" \
        "both files have left the to-do set -- delete this block, do not assert an empty row"
    failures=$((failures + 1))
fi
# editor.cpp is now DELIBERATELY absent, the EIGHTH file to exercise the rule the
# export_html.cpp note below states -- it compiles off Windows and joined the manifest, so its
# 3,207 lines left the to-do set and therefore left here. It is the largest single departure this
# gate has processed, ahead of overlays.cpp's 4,153 only because that one left before this claim
# existed to be deleted.
#
# IT IS THE FOURTH CONSECUTIVE INSTANCE OF THE SIZE-ROW TRAP, AND THE FIRST WHERE THE GROWTH IS
# SMALL ENOUGH TO SHOW WHAT THE TRAP ACTUALLY SCALES WITH. 3,135 -> 3,207, +72 lines, +2.3% --
# against drafts.cpp's +74%, signals.cpp's +5.4% and utils.cpp's +5.6%. The write-up of a port is
# roughly proportional to the number of DECISIONS in it, not to the size of the file, so the
# relative growth shrinks as the file grows while the absolute figure stays in the same tens of
# lines. A reader who took +74% as the rule would predict 5,400 lines here and be wrong by 2,200.
# The direction is the property; the magnitude is not.
#
# annotations.cpp is DELIBERATELY absent from here as of 2026-09-17, by the same rule the
# export_html.cpp note below states: it left the to-do set, so a derived claim about its line
# count would keep asserting a README figure that must no longer exist. It is the FOURTH file
# to exercise that rule and the first to leave the table EMPTY -- math_render.cpp, overlays.cpp
# and tableedit.cpp had already gone, so annotations.cpp was the row's last member and the row
# now carries a "--" where its size used to be. A row whose figure is gone cannot be derived,
# which is why this is a comment and not a weaker claim.
#
# IT ALSO LEFT ON THE NARROWER OF THE TWO CLAIMS, which matters to whoever re-adds a row here:
# annotations.cpp COMPILES off Windows and links nowhere, so it sits in the manifest's
# compile-only section. overlays.cpp set that precedent; export_html.cpp and math_render.cpp
# left on the stronger compile-link-run reading. The manifest keys on compilation and the
# to-do set is derived from the manifest, so this table cannot tell the two apart -- the README
# prose has to, and it does, in the paragraph above the table.
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
# pandoc.cpp is now DELIBERATELY absent too, the FOURTH file to exercise the rule the
# export_html.cpp note below states, and the first to be caught by this gate in the same
# run that freed it. It reached 0 errors and joined the manifest's compile-only section on
# 2026-09-20 when the process seam took its last Win32 call, so it left the to-do set and
# therefore left here. What makes it worth a note of its own is the DIRECTION the stale
# figure would have pointed: the port GREW the file 276 -> 323 lines, because it added the
# header comment explaining the masking rule that had understated it. So a row still
# deriving from pandoc.cpp would have reported the remaining Windows work in exports and
# printing going UP by 47 lines on the slice that finished the file -- the same inversion
# the utils.cpp row records, where a proxy for remaining work grows at the moment the work
# goes away. The label is a claim here too.
# export_docx.cpp IS NOW DELIBERATELY ABSENT AS WELL, the EIGHTH file to exercise the rule
# the export_html.cpp note below states, and the only one so far that left the to-do set
# without anyone porting it. Its 1,545 lines already compiled off Windows; three unused
# includes -- <objbase.h>, <ole2.h>, <wincodec.h>, left behind by the 2026-09-19 clipboard
# slice -- made the census stop at the first line and report `fatal 1, errors 0`. Removing
# them took the file to exit 0 and onto tests/portable-sources.txt.
# SO THIS ROW IS DOWN TO print.cpp ALONE AND ITS FIGURE HAS NO SECOND TERM. Worth noting
# against the pandoc.cpp inversion above: this is the opposite direction and the same
# lesson. There the proxy grew when the work went away; here the proxy was never measuring
# work at all -- 1,545 lines sat in a "remaining Windows work" figure while being portable
# C++, because the row derives from `wc -l` of a file the to-do TABLE listed, and nothing
# rechecked whether the table was right.
# export_html.cpp is DELIBERATELY absent from this row: it left the to-do set when it
# reached 0 errors and joined tests/portable-sources.txt, so deriving the row from it
# would keep asserting a README figure that must no longer include it. This row is
# hand-maintained against the to-do TABLE, which the manifest does not drive -- so it
# is the one row that can rot silently in the *other* direction, by keeping a file the
# tree has since freed. When a file leaves the to-do set, it leaves here too.
claim "exports and printing"  "| $(c "$(lines src/print.cpp)") |"
# tabs.cpp is DELIBERATELY absent too, one more file exercising the rule, and this row's last
# member, so the shell-integration claim that derived from it is gone with it. It left the to-do
# set on 2026-09-23 by joining tests/portable-sources.txt's compile-only section at errors 0. One
# commit earlier the README said it stayed off the manifest "because it does not link"; that was
# the wrong test, and the claim here kept asserting 1,622 lines of remaining work for as long as it
# was believed.
# drafts.cpp is now DELIBERATELY absent from the row above as well, the SEVENTH file to
# exercise the rule -- and it is the reading that settles the direction question, because the
# growth was not marginal. 160 -> 279 lines, +74%, on the pass that FINISHED the file. The
# other two were 179 -> 189 and 837 -> 882, small enough to be dismissed as noise; +119 lines
# cannot be. Three consecutive passes, three files, one direction, and the mechanism is not
# incidental to porting -- it IS porting, as this tree does it: the port is five spellings and
# the write-up of why each one is what it is, so the write-up is most of the diff. Any row
# derived from a file's SIZE therefore moves the WRONG WAY as the work completes, and the
# larger the share of the change that is reasoning, the wronger it moves. The rule below is
# the fix and it now has seven instances.
#
# signals.cpp is DELIBERATELY absent, the SIXTH file to exercise the rule; its claim was
# `wc -l src/signals.cpp` and the port GREW it 837 -> 882.
#
# utils.cpp is DELIBERATELY absent, and it is the FIFTH file to exercise the rule the
# export_html.cpp note above states -- it compiles off Windows and joined the manifest, so it
# left the to-do set and therefore left here. Its row is the SECOND to empty, after Annotations.
#
# IT IS ALSO THE CLEAREST CASE YET FOR WHY THE RULE IS NOT BOOKKEEPING, because this row's
# figure moved in the REASSURING direction on the pass that finished the file. The claim was
# `wc -l src/utils.cpp`, i.e. the size of the file rather than the size of the debt, and the
# split GREW it 179 -> 189 by adding comments recording what left. So a row that had gone stale
# by one pass would have asserted 179 against a tree reading 189 and failed here honestly; a row
# left DERIVED reads 189 and passes, and publishes a bigger number for less remaining work. A
# derived figure is only as good as what it is a proxy for, and a line count stops being a proxy
# for remaining work at exactly the moment the file stops being remaining work.
#
# The other four left on this rule and it is worth keeping the two readings apart, since the
# manifest keys on compilation: export_html.cpp and math_render.cpp left on the strong
# compile-link-run claim; overlays.cpp, annotations.cpp, utils.cpp and now signals.cpp left on
# the narrow compile-only one.
#
# THE CLAUSE THAT USED TO END THIS NOTE SAID "utils.cpp is one symbol from the strong claim --
# signalPushKey, in src/signals.cpp". That symbol is closed and utils.cpp did not move, because
# src/signals.cpp arrived carrying three undefined symbols of its own (drafts.cpp, editor.cpp).
# "One symbol from" invited exactly the reading it got -- that closing it would promote the
# file -- so the shape to write is the symbol and its file, never a distance to the goal.
# startpage.cpp is DELIBERATELY absent, the NINTH file to exercise the rule the export_html.cpp
# note above states -- it compiles off Windows and joined the manifest, so it left the to-do set
# and therefore left here. Its row is the FOURTH to empty, after Annotations, Clipboard and Editor.
#
# IT IS THE MOST EXTREME INSTANCE OF THE SIZE-ROW TRAP THIS GATE HAS PROCESSED, AND THE FIRST
# WHERE LEAVING AND GROWING HAPPENED IN THE SAME PASS. The claim was `wc -l src/startpage.cpp`;
# the file was 792 lines when the pass opened and is 898 now, +106, +13.4% -- so had this claim
# been left derived and the row left in the table, the README would have published 898 as
# remaining work for a file with none, and the gate would have PASSED on it. Every earlier
# instance was caught by the figure going stale in one direction or the other; this one would not
# have gone stale at all. A derived figure whose derivation is still correct and whose MEANING has
# gone is invisible to every instrument in this repository, which is why the rule is "when a file
# leaves the to-do set, it leaves here too" and not "re-derive the row".
#
# The +13.4% also lands where drafts.cpp's +74%, editor.cpp's +2.3%, signals.cpp's +5.4% and
# utils.cpp's +5.6% predicted it would: between the small-file and large-file ends, on a 792-line
# file. Five instances now, one direction, and the magnitude tracks the number of DECISIONS in the
# port rather than the size of the file -- this pass had two that needed real argument (a
# reentrant local-time seam and a signed FILETIME intermediate), so it grew more than editor.cpp's
# 3,135 lines did on eight routed families.
claim "device and factory"    "| Device and factory creation | $(c "$(lines src/d2d_init.cpp)") |"

# THE MEMBERSHIP OF THIS ROW IS DERIVED NOW, AND THE REASON IS A MISS THIS SCRIPT OWNS.
# It used to be a hand-written list of seven names:
#
#     backends="render_d2d text_d2d settings_win32 platform_paths_win32 settings_app
#               keyboard_win32 image_win32"
#
# and it omitted file_dialog_win32.cpp for several passes. The failure was invisible from
# both ends: the README's line total was CORRECT for the seven files listed, so the row was
# internally consistent, and this script derived that same total from the same seven names,
# so the checker agreed with the prose about a set that was missing a member. Deriving a
# figure from a hand-written membership list buys nothing the list does not already give.
#
# So the membership comes off the to-do set by NAME SHAPE -- every file whose stem ends
# _win32 or _d2d -- which is the naming convention every seam's Windows half in this tree
# already follows, and which makes the next one join this row by existing. Note the two
# things the pattern deliberately does NOT catch: d2d_init.cpp, whose stem starts with d2d
# rather than ending in it and which has its own row, and settings_app.cpp, the one member
# that predates the convention. That one is still named by hand -- and it is ASSERTED to be
# in the to-do set below, because a hand-written name that silently stops matching is the
# failure this comment is about.
#
# The expression is SORTED, so the README row is in derivation order rather than arrival
# order. That loses a small story the old row told and it is the correct trade: an order
# nothing derives is one more thing that can rot while reading as deliberate.
#
# THE PATTERN OVER-CAUGHT ON ITS FIRST RUN and this script's own output is what caught it:
# main_d2d.cpp ends _d2d.cpp, so it walked into this row and took the total from 2,279 to
# 4,162, while already being counted in the "Window and message loop" row. It is the Windows
# *frontend*, not a backend behind a seam, and a name-shape rule cannot tell those apart. So
# there is one exclusion, by name, and it gets the same guard the one inclusion gets: both
# hand-written names are asserted to still be in the to-do set, because a hand-written name
# that silently stops matching is precisely the failure this whole comment is about.
backends_excluded="main_d2d.cpp"
backends=$( { printf '%s\n' $todo | grep -E '(_win32|_d2d)\.cpp$' \
                | grep -vxF "$backends_excluded"; echo settings_app.cpp; } \
            | sed 's/\.cpp$//' | sort )
for hand in settings_app.cpp $backends_excluded; do
    if ! printf '%s\n' $todo | grep -qx "$hand"; then
        echo "  FAIL backends list       $hand is hand-named here and is NOT in the to-do set"
        failures=$((failures + 1))
    fi
done
expr_backends=""
raw_backend_lines=0
for f in $backends; do
    n=$(lines "src/$f.cpp")
    raw_backend_lines=$((raw_backend_lines + n))
    expr_backends="${expr_backends:+$expr_backends + }$(c "$n")"
done
# NOTE ON THE CONTROL, since this is the one claim whose components are not all bumped:
# nbackends deliberately is not, because it is a count of a DERIVED set rather than a
# measurement of a file, so "bump it by one" would assert a membership that does not exist.
# The consequence is honest and worth stating: in a control run the "$nbackends files" claim
# misses on its LINE TOTAL alone, so the file count itself is un-controlled. It is covered by
# the real run instead -- a membership change moves the additive expression above, which is
# the strong check, and no coincidence of line counts can hide a file entering or leaving it.
nbackends=$(printf '%s\n' $backends | grep -c . || true)
claim "the Windows backends (derived membership)" "| $expr_backends |"
claim "  ... and their total"        "$nbackends files, $(c "$raw_backend_lines") lines"
claim "  ... quoted again in prose"  "$(c "$raw_backend_lines") lines of it today"
# A THIRD SITE, ADDED 2026-09-18 AFTER IT WAS FOUND STALE BY 237 LINES. This gate already
# asserted the backends total twice and the README quoted it three times -- the third being in
# the "roughly a third of the way through" paragraph, which is in a DIFFERENT SECTION of the
# file. That is how the blind spot was made: the gate was written by walking the completeness
# section, so a checker that greps the whole README was aimed at part of it. The claim was
# reassuringly low, like every stale figure this document has caught, and nothing in the repo
# could see it. When a figure gains a quotation site, it gains a claim here.
claim "  ... and a THIRD time, other section" "$(c "$raw_backend_lines") lines of Windows backend are"

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
#
# THE VERB IS DERIVED, and it has to be: this figure reached 1 on 2026-09-18 and the
# generator hardcoded "calls remain", so a green run required the README to publish
# "1 calls remain in the tree". A gate that can only be satisfied by bad prose gets
# edited around rather than obeyed, which is the failure mode worth spending four
# lines to avoid. Pluralisation is the only thing derived here -- the figure is not.
grep_total=$(grep -rn 'dwriteFactory->CreateTextLayout' src/ | grep -c . || true)
comment_hits=$(grep -rn 'dwriteFactory->CreateTextLayout' src/ | grep -cE ':[[:space:]]*//' || true)
live_calls=$((grep_total - comment_hits))
if [ "$live_calls" -eq 1 ]; then verb="call remains"; else verb="calls remain"; fi
claim "direct CreateTextLayout calls" \
      "**$(bump "$live_calls") direct \`dwriteFactory->CreateTextLayout\` $verb in the tree**"
claim "  ... vs what a bare grep returns" \
      "\`grep\` for the call returns $(bump "$grep_total")"

# --- is tests/test-platforms.txt telling the truth? -------------------------
# D and E above are arithmetic over that file, so the file is now load-bearing and an unchecked
# manifest would just move the rot one hop. Three things are asserted here; a fourth, that the
# manifest matches what a non-Windows configure actually produces, is the Linux end of the same
# claim and lives in scripts/probe-portable-linux.sh, because this script cannot see that host.
#
# ON A CONTROL RUN THESE ARE SKIPPED, not bumped. There is nothing to perturb: they compare two
# sets and a count, and "bump one" would turn a correct manifest into an incorrect one and assert
# that the gate notices -- which is a mutation test of a different shape, better run by hand than
# smuggled into the arm whose pass condition is failures == checks.
mfail() { printf '  FAIL %-42s %s\n' "$1" "$2"; platform_failures=$((platform_failures + 1)); }
mok()   { printf '  ok   %-42s %s\n' "$1" "$2"; }
mskip() { printf '  SKIP %-42s %s\n' "$1" "$2"; platform_skips=$((platform_skips + 1)); }

echo
if [ "$control" -eq 1 ]; then
    echo "-- tests/test-platforms.txt fidelity: not run on a control arm (nothing here is perturbable)"
else
    echo "-- tests/test-platforms.txt fidelity"

    # (1) Every row that is not `both` carries a reason whose first token is a known KIND. A row
    # with a missing or unknown kind would silently fall out of D's denominator, because the
    # subject_winonly count keys on the literal `subject:`.
    bad_rows=$(printf '%s\n' "$platform_rows" \
        | awk '$2!="both" && $3!="sources:" && $3!="subject:" {print $1" ["$2"] reason="$3}')
    bad_tokens=$(printf '%s\n' "$platform_rows" \
        | awk '$2!="both" && $2!="windows-only" && $2!="linux-only" {print $1" platform="$2}')
    if [ -n "$bad_rows$bad_tokens" ]; then
        mfail "manifest rows well-formed" "$(printf '%s %s' "$bad_rows" "$bad_tokens")"
    else
        mok "manifest rows well-formed" "$((both_count + winonly_count + $(pcount linux-only))) rows, every non-both row has a sources:/subject: reason"
    fi

    # (2) The manifest's both + windows-only set IS the Windows ctest list, both directions. The
    # generated CTestTestfile.cmake is the authority and CMakeLists.txt is not: a static parse of
    # the source read `if(WIN32 OR Qt6_FOUND)` as Windows-only and so reported 37 portable tests
    # against a measured 39, one of three ways a hand parse went wrong in a single hour.
    #
    # EXACTLY ONE CANDIDATE, AND NO FALLBACK LIST. The first version of this tried
    # `build-win-split build build-win-release` in turn, and the mutation arm that hid
    # build-win-split did not skip -- it found a stale sibling and reported 16 phantom diffs, so an
    # UNMEASURABLE state read as a loud wrong answer instead of a quiet honest one. The `.vcxproj`
    # probe only proves a directory is an MSBuild configure, never that it is a CURRENT one, and
    # build-linux/ in this same tree is the standing proof that a configured build dir can be
    # stale by 11 tests and say nothing. So: one named directory, overridable, and anything else
    # is a skip.
    win_build=""
    win_build_why=""
    cand="${TINTA_WIN_BUILD_DIR:-build-win-split}"
    if [ ! -f "$cand/CTestTestfile.cmake" ]; then
        win_build_why="$cand has no CTestTestfile.cmake -- not configured, or this host is not Windows"
    else
        vcx_probe=$(printf '%s\n' "$cand"/*.vcxproj | head -1)
        if [ ! -f "$vcx_probe" ]; then
            win_build_why="$cand is configured but carries no *.vcxproj, so it is not the MSBuild arm (a Ninja or Linux configure of this same tree looks exactly like this)"
        else
            win_build="$cand"
        fi
    fi

    if [ -z "$win_build" ]; then
        mskip "manifest vs the Windows ctest list" "$win_build_why -- set \$TINTA_WIN_BUILD_DIR to name it; off Windows this skip is the expected state"
        mskip "every windows-only reason is true"  "same reason: the source census reads that build dir's *.vcxproj"
    else
        ctest_names=$(grep -oE '^[[:space:]]*add_test\([A-Za-z0-9_]+' "$win_build/CTestTestfile.cmake" \
            | sed -E 's/.*add_test\(//' | sort -u)
        manifest_win=$(printf '%s\n' "$platform_rows" | awk '$2=="both"||$2=="windows-only" {print $1}' | sort -u)
        only_manifest=$(comm -23 <(printf '%s\n' "$manifest_win") <(printf '%s\n' "$ctest_names") | tr '\n' ' ')
        only_ctest=$(comm -13 <(printf '%s\n' "$manifest_win") <(printf '%s\n' "$ctest_names") | tr '\n' ' ')
        if [ -n "$(printf '%s%s' "$only_manifest" "$only_ctest" | tr -d ' ')" ]; then
            mfail "manifest vs the Windows ctest list" "in manifest only: ${only_manifest:-<none>}| in ctest only: ${only_ctest:-<none>}"
        else
            mok "manifest vs the Windows ctest list" "$(printf '%s\n' "$manifest_win" | grep -c .) names, equal both directions ($win_build)"
        fi

        # (3) THE CHECK THAT WOULD HAVE CAUGHT THE SEVEN. For each windows-only row, count the
        # src/*.cpp the target actually compiles that are NOT in tests/portable-sources.txt -- read
        # out of CMake's generated project file, so ${SOURCES}, ${INPUT_TEST_SOURCES} and
        # ${LEGACY_D2D_SOURCES} are already expanded. ${PLATFORM_PATHS_SOURCE} is excluded by name:
        # that slot is SELECTED per platform (platform_paths_win32.cpp here, platform_paths_qt.cpp
        # off Windows), so it is never what strands a target.
        # A `sources:` row must state a count EQUAL to the measured one; a `subject:` row must
        # measure exactly zero. Both directions, because the failure this replaces was a
        # `sources:`-shaped claim written over targets whose blockers nobody had counted.
        #
        # EXACT EQUALITY AND NOT `>= 1`, and that is the difference between this being a check and
        # being a formality. The twelve `claim "blockers: …"` calls far above read their number out
        # of THIS column, so a `>= 1` census would let the manifest say `sources: 99` and the README
        # publish 99 blockers for a target that compiles seventeen -- green at both ends, wrong on
        # the page, which is precisely the rot the whole slice exists to stop. The stated count is
        # what makes the hop from build fact to published figure, so the stated count is what has
        # to be right, not merely non-zero.
        #
        # `read` TAKES THE PLATFORM COLUMN TOO, and the discard is named rather than folded into
        # the trailing catch-all. Written as `read -r name kind _rest` this loop put the string
        # `windows-only` into `kind`, so NEITHER branch below could ever fire: it walked all twelve
        # rows, counted them, and asserted nothing -- green against a manifest deliberately
        # mislabelled with the exact refuted claim. The `else` at the bottom exists for the same
        # reason: an unrecognised kind is a failure, never a row that quietly falls through.
        census_bad=""
        census_skip=""
        census_rows=0
        while read -r name _platform kind rest; do
            [ -n "$name" ] || continue
            tgt=$(sed -nE "s@^[[:space:]]*add_test\($name .*/([A-Za-z0-9_]+)\.exe.*@\1@p" \
                "$win_build/CTestTestfile.cmake" | head -1)
            if [ -z "$tgt" ] || [ ! -f "$win_build/$tgt.vcxproj" ]; then
                # NOT a failure: no project file means the blocker count is UNKNOWN, and an unknown
                # reported as a contradiction is the same error in the other direction.
                census_skip="$census_skip $name"
                continue
            fi
            n_block=$(grep -o 'ClCompile Include="[^"]*"' "$win_build/$tgt.vcxproj" \
                | sed -e 's/.*Include="//' -e 's/"$//' | tr '\\' '/' \
                | grep -E '/src/[A-Za-z0-9_]+\.cpp$' | sed 's@.*/src/@@' | sort -u \
                | grep -v '^platform_paths_win32\.cpp$' \
                | while read -r b; do grep -qxF "$b" tests/portable-sources.txt || echo "$b"; done \
                | grep -c . || true)
            census_rows=$((census_rows + 1))
            case "$kind" in
                sources:)
                    stated=$(printf '%s' "$rest" | awk '{print $1}')
                    case "$stated" in
                        ''|*[!0-9]*)
                            census_bad="$census_bad $name(sources: reason does not start with a count, it starts with '${stated:-<nothing>}')" ;;
                        0)
                            census_bad="$census_bad $name(says sources: 0, which is a subject: row wearing the wrong kind)" ;;
                        *)
                            [ "$stated" -eq "$n_block" ] \
                                || census_bad="$census_bad $name(says sources: $stated but compiles $n_block non-portable src/*.cpp)" ;;
                    esac ;;
                subject:)
                    [ "$n_block" -eq 0 ] \
                        || census_bad="$census_bad $name(says subject: but compiles $n_block non-portable src/*.cpp)" ;;
                *)
                    census_bad="$census_bad $name(unrecognised reason kind '$kind')" ;;
            esac
        done <<EOF
$(prow windows-only)
EOF
        if [ -n "$census_bad" ]; then
            mfail "every windows-only reason is true" "$census_bad"
        elif [ "$census_rows" -eq 0 ]; then
            mskip "every windows-only reason is true" "no windows-only row was measurable, so this claim took no reading at all"
        else
            mok "every windows-only reason is true" "$census_rows targets censused against tests/portable-sources.txt"
        fi
        [ -z "$census_skip" ] \
            || mskip "  ... blocker count unknown for" "$census_skip (no *.vcxproj in $win_build; those rows are unmeasured, not agreed with)"
    fi
fi

# --- the reading, failures FIRST -------------------------------------------
echo
echo "readme_figure_failures=$failures"
echo "readme_figures_checked=$checks"
echo "platform_manifest_failures=$platform_failures"
echo "platform_manifest_skips=$platform_skips"
if [ "$control" -eq 1 ]; then
    if [ "$failures" -eq "$checks" ]; then
        echo "CONTROL PASSED: all $checks claims missed when perturbed, so each is anchored to its own sentence"
        exit 0
    fi
    echo "CONTROL FAILED: $((checks - failures)) perturbed claim(s) still matched -- those matches are too loose to trust"
    exit 1
fi
[ "$failures" -eq 0 ] && [ "$platform_failures" -eq 0 ] \
    && echo "All $checks derived claims are the ones the README states, and tests/test-platforms.txt agrees with the build."
# The platform failures are folded into the exit status as well as printed. The PRINTED line is
# still the authority for tests/run_figure_gate.cmake -- but a hand run that exits 0 with a FAIL
# line above it is a silent instrument, and the whole reason this block exists is that a figure
# nothing checked went stale in a table a reader trusts.
exit $((failures + platform_failures))

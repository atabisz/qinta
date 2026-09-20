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
claim "shell integration"     "| $(c "$(lines src/tabs.cpp)") |"
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

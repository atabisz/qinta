#!/usr/bin/env bash
# check-portable-claims.sh -- assert that no comment in this tree calls a file unportable when
# tests/portable-sources.txt says it compiles off Windows.
#
# WHY THIS EXISTS, and it is a measured reason rather than a precaution. On 2026-09-22 one slice
# corrected TWELVE comments that named the wrong blocker, and closed a criterion asserting the
# sweep was complete across CMakeLists.txt, src/, include/, tests/, scripts/ and README.md. A
# thirteenth survived in README.md -- "all defined in src/math_render.cpp, which does not compile
# off Windows (34 errors, IDWriteTextLayout incomplete)" -- about a file that compiles at 0 errors
# and is in the manifest. It survived for a reason worth writing down: the sweep's phrase set was
# derived from the phrasings it had already found in CMakeLists.txt, and README words the same
# claim differently. A CENSUS KEYED ON THE SHAPE YOU HAVE ALREADY FIXED IS BLIND TO THE SHAPE YOU
# HAVE NOT. The same claim had two homes, the build-system copy was corrected, and the corrected
# copy is what kept the README copy stale -- a reader who checks CMakeLists finds the truth and
# never comes back.
#
# WHAT IT ASSERTS, narrowly, because the narrowness is what makes it decidable: for each of the
# manifest's entries, no two-line window anywhere in the tracked corpus may pair that entry's
# basename with an assertive non-portability phrase, unless that exact line is listed in
# tests/portable-claims-allow.txt with a reason.
#
# WHAT IT DOES NOT ASSERT, stated here because an exit code reads as coverage and this one is not:
#
#   * It knows nothing about claims phrased in words the phrase list does not carry. That is the
#     hole the README site fell through, and a gate cannot close it -- a new phrasing is new. What
#     the gate DOES buy is that the phrasings already seen in this tree cannot come back, and that
#     PHRASES is a checked-in list a reader can extend rather than a regex nobody re-derives.
#   * It says nothing about files OUTSIDE the manifest. A true claim that src/print.cpp does not
#     build off Windows must stay green, and does -- print.cpp is not a manifest entry. That is
#     the counter-case, and it is what stops this gate from being a grep for the word "fatal".
#   * A manifest entry only ever claimed the file COMPILES off Windows. It never claimed the file
#     LINKS into the Qt app, and a comment saying "in the manifest but cannot link into qinta" is
#     TRUE. Those live in the allowlist under reason `true-claim`, which is the one reason token
#     that is load-bearing rather than bookkeeping.
#
# THE ALLOWLIST IS NOT A SILENCER. Three properties are asserted about it, and the second is the
# one that matters most:
#
#   1. Every row needs a reason from a closed set and a non-empty note. An unrecognised reason
#      fails in its own words.
#   2. A row that matches NOTHING fails as stale. The population here SHRINKS when the tree is
#      fixed -- a correction record can be reworded, a quotation closed, a file dropped from the
#      manifest -- so an entry that has stopped applying would otherwise expire in silence and
#      leave the allowlist reading as though it were still holding something back.
#   3. Rows are keyed on NORMALISED LINE TEXT, never on a line number. A line citation in this
#      tree has been wrong far more often than right; the text moves with the line.
#
# Usage:
#   scripts/check-portable-claims.sh            # subject arm: expect 0 failures
#   scripts/check-portable-claims.sh --control  # control arm: expect failures == hits, hits > 0
#
# Exit 0 pass, 1 fail. Reads nothing outside the repo and writes nothing.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT" || { echo "portable_claims: cannot enter repo root"; exit 1; }

MANIFEST="tests/portable-sources.txt"
ALLOWLIST="tests/portable-claims-allow.txt"

CONTROL=0
if [ "${1:-}" = "--control" ]; then CONTROL=1; fi

# THE CONTROL ARM DROPS THE ALLOWLIST RATHER THAN PERTURBING A NUMBER. With no allowlist every
# real hit is unexcused, so `failures == hits` is the arm's invariant and a hits count of 0 means
# the matcher never fired -- which is the failure this arm exists to catch. Selected by the
# ARGUMENT, not by an environment variable: a control selected by an env var has been silently
# inert in this tree before.
if [ "$CONTROL" = "1" ]; then ALLOWLIST="/dev/null"; fi

# Floors, not tracking counts. A floor that follows the tree rots at every commit and has to be
# maintained; these two only have to catch "measured nothing", which is the failure mode a green
# exit cannot otherwise distinguish from success.
MIN_ENTRIES="${PORTABLE_CLAIMS_MIN_ENTRIES:-40}"
MIN_FILES="${PORTABLE_CLAIMS_MIN_FILES:-40}"

if [ ! -f "$MANIFEST" ]; then
    echo "portable_claims: FAIL -- $MANIFEST is missing, so there is no population to check"
    exit 1
fi

# The same parser scripts/check-readme-figures.sh uses, deliberately: two different readings of
# what "an entry" means would let a file be in one population and not the other.
entries=$(grep -vE '^[[:space:]]*(#|$)' "$MANIFEST" | sort -u)
entry_count=$(printf '%s\n' "$entries" | grep -c . || true)

# The corpus is git's list, so a build directory or an untracked scratch file cannot join it and
# cannot be missed from it either. Everything here is text a human wrote.
files=$(git ls-files -- '*.cpp' '*.h' '*.md' '*.sh' 'CMakeLists.txt' 'tests/*.txt' \
        | grep -vE '^(third_party|external)/' | sort -u)
file_count=$(printf '%s\n' "$files" | grep -c . || true)

if [ "$entry_count" -lt "$MIN_ENTRIES" ]; then
    echo "portable_claims: FAIL -- $entry_count manifest entries, floor is $MIN_ENTRIES"
    echo "portable_claims_entries=$entry_count"
    exit 1
fi
if [ "$file_count" -lt "$MIN_FILES" ]; then
    echo "portable_claims: FAIL -- $file_count corpus files, floor is $MIN_FILES"
    echo "portable_claims_files=$file_count"
    exit 1
fi

# THE PHRASE LIST IS DATA, one per line, matched case-insensitively against a two-line window.
# Every one of these is a phrasing that HAS appeared in this tree -- derived 2026-09-22 by
# censusing the whole corpus rather than one file, which is the correction this gate embodies.
# Adding to it is cheap and is meant to be done from a real site, not invented.
#
# IT IS A COMPILATION LIST, AND THAT IS THE WHOLE DESIGN. A manifest entry asserts exactly one
# thing -- the file compiles off Windows -- so the only claim that can CONTRADICT membership is a
# claim about compiling. `cannot link`, `does not link` and `never link Qt` were in the first
# draft of this list and were measured out of it: they produced 30-odd hits of which the large
# majority were TRUE, because this tree's manifest has a compile-only section full of entries that
# genuinely cannot link into a portable target. A gate whose allowlist is mostly `true-claim` is
# an inventory, not a check. Excluding them costs nothing that manifest membership was ever
# evidence about, and it is the same compiles-vs-links distinction whose conflation caused the
# thirteen wrong comments in the first place.
#
# `windows-only file` and `fatal include` came out for a different reason: both appear far more
# often in TRUE statements about non-entries and in negated sentences ("no longer a Windows-only
# file at all") than in false claims, and neither asserts non-compilation on its own.
PHRASES='does not compile
doesn.t compile
cannot compile
can not compile
never compiled
never compiles
not compile off
does not build
doesn.t build
cannot build
will not build
never builds
fails to compile
fails to build
no non-windows compiler
dies off windows
dies on <
compile off windows at all
not portable
unportable
[a-z0-9_]+\.h-fatal'

tmp_hits=$(mktemp)
tmp_report=$(mktemp)
trap 'rm -f "$tmp_hits" "$tmp_report"' EXIT

# ONE AWK PASS PER FILE, and the shape is chosen for a measured reason: a bash loop with
# paragraph scope over this corpus timed out at 600s on an earlier attempt. Basenames are matched
# with a LEFT WORD BOUNDARY because `utils.cpp` matches inside `file_utils.cpp` and an unanchored
# census inflates.
#
# DEDUPLICATION IS PART OF THE INSTRUMENT, not a tidy-up. A sliding two-line window reports a
# single-line site TWICE -- once as the window's second line and once as its first -- so an
# earlier hand-run of this matcher printed 37 windows for what a prediction had put at 12-20
# SITES. The units differed, not the populations. Here a LINE is reported at most once: it hits
# if it carries both halves itself, or if it carries one half and the NEXT line carries the other.
printf '%s\n' "$entries" > "$tmp_hits.entries"
printf '%s\n' "$PHRASES" > "$tmp_hits.phrases"

printf '%s\n' "$files" > "$tmp_hits.files"

# ONE AWK INVOCATION OVER THE WHOLE CORPUS, not one per file. The per-file loop cost 47 s on
# Windows with `sys` three times `user` -- 250 process spawns, which this host charges heavily for
# and which no amount of regex tuning touches. Files come in on the command line and FILENAME
# tells them apart.
#
# -v, NOT a trailing `ENTFILE=...` operand: a positional assignment is processed when awk REACHES
# it in the argument list, so it is unset during BEGIN and the getline reads a null filename. That
# failed loudly here, which is the only reason it cost one run instead of a green on an empty
# corpus.
#
# The dedup runs with a ONE-LINE DELAY rather than by buffering each file, so nothing is held in
# memory across a 4,000-line source: when line i arrives, line i-1 can be finalised, because a
# hit at i-1 depends only on i-1 and i. A file boundary finalises the previous file's last line
# with only its own two halves, which is correct -- a claim cannot span two files.
# shellcheck disable=SC2046
awk -v ENTFILE="$tmp_hits.entries" -v PHRFILE="$tmp_hits.phrases" '
    # ONE COMBINED ALTERNATION PER SIDE, not a loop over the list, and that is a measured
    # decision rather than a style one. The first version tested each of the 68 basenames
    # separately against each of ~60,000 lines -- four million regex matches -- and the ctest arm
    # read 105 s, which would have made this pair the most expensive family in the suite and the
    # comment registering it in CMakeLists.txt said "about two seconds". Collapsing both lists to
    # a single regex each takes it to two matches per line.
    BEGIN {
        while ((getline line < ENTFILE) > 0) {
            if (line == "") continue
            esc = tolower(line)
            gsub(/\./, "[.]", esc)
            baseRe = (baseRe == "") ? esc : baseRe "|" esc
        }
        close(ENTFILE)
        # The left boundary is what stops `utils.cpp` matching inside `file_utils.cpp`. No RIGHT
        # boundary: the extension already ends the token, and requiring one would miss a basename
        # immediately followed by punctuation, which is most of them in prose.
        baseRe = "(^|[^a-z0-9_])(" baseRe ")"
        while ((getline line < PHRFILE) > 0) {
            if (line == "") continue
            phrRe = (phrRe == "") ? tolower(line) : phrRe "|" tolower(line)
        }
        close(PHRFILE)
    }
    # The key is the normalised text of the reported line: comment markers, quoting punctuation
    # and indentation stripped, whitespace collapsed, lowercased. Nothing about the line NUMBER
    # survives into the key, deliberately.
    function emit(file, lineno, text, hasBase, hasPhrase, nextBase, nextPhrase) {
        if (!((hasBase && hasPhrase) || (hasBase && nextPhrase) || (hasPhrase && nextBase))) return
        key = tolower(text)
        gsub(/^[ \t]*([#*>]|\/\/|--)+[ \t]*/, "", key)
        gsub(/[ \t]+/, " ", key)
        gsub(/^ +| +$/, "", key)
        if (key == "") return
        printf "%s\t%d\t%s\n", file, lineno, key
    }
    FNR == 1 {
        # The previous file has ended, so its last held line can only pair with itself.
        if (pf != "") emit(pf, pn, pt, pb, pp, 0, 0)
        pf = ""
    }
    {
        low = tolower($0)
        cb = (low ~ baseRe)
        cp = (low ~ phrRe)
        if (pf != "") emit(pf, pn, pt, pb, pp, cb, cp)
        pf = FILENAME; pn = FNR; pt = $0; pb = cb; pp = cp
    }
    END {
        if (pf != "") emit(pf, pn, pt, pb, pp, 0, 0)
    }
    ' $(cat "$tmp_hits.files") > "$tmp_hits"

hits=$(grep -c . "$tmp_hits" || true)
failures=0
checked=0
stale=0

# Allowlist rows: reason <TAB> normalised-text <TAB> why.
allow_keys=$(mktemp); allow_bad=$(mktemp)
trap 'rm -f "$tmp_hits" "$tmp_hits.entries" "$tmp_hits.phrases" "$tmp_hits.files" "$tmp_report" "$allow_keys" "$allow_bad"' EXIT

VALID_REASONS='correction quotation history true-claim co-occurrence'

# PARSED IN AWK RATHER THAN IN A SHELL LOOP, and it is the same measured reason as the corpus
# pass: three `cut` spawns per row plus a `grep` per hit and a `cut | grep` per row came to about
# two hundred processes, which is most of the remaining `sys` time on this host. Reason validity,
# key presence and note presence are all decided here in one pass.
allow_rows=$(awk -F'\t' -v KEYS="$allow_keys" -v BAD="$allow_bad" -v VALID="$VALID_REASONS" '
BEGIN { n = split(VALID, vs, " "); for (i = 1; i <= n; i++) valid[vs[i]] = 1 }
/^[ \t]*$/ { next }
/^#/ { next }
{
    rows++
    good = 1
    if (!($1 in valid)) {
        printf "row %d: reason \"%s\" is not one of: %s\n", rows, $1, VALID >> BAD
        good = 0
    }
    if ($2 == "") {
        printf "row %d: empty key -- rows are <reason>TAB<normalised text>TAB<why>\n", rows >> BAD
        good = 0
    }
    # NF < 3 rather than a concatenation test: a row with a tab and nothing after it should fail
    # for the same reason as a row with no third column at all.
    if (NF < 3 || $3 == "") {
        printf "row %d: no note -- an excused claim must say why it is excusable\n", rows >> BAD
        good = 0
    }
    # The FILE row number travels with the key, so a STALE report names the row a reader has to
    # open rather than an index into the surviving subset.
    if (good) printf "%d\t%s\n", rows, $2 >> KEYS
}
END { print rows + 0 }
' "$ALLOWLIST")

# ASSERTIONS 1 AND 2 IN ONE PASS, because they are two directions of the same set comparison:
# every hit must be excused by a key, and every key must be matched by a hit. The second is the
# one that stops this file rotting -- the population SHRINKS when the tree is fixed, so a row
# whose site was corrected would otherwise sit here reading as though it still excused something.
xref=$(awk -F'\t' -v KEYS="$allow_keys" -v ALLOWFILE="$ALLOWLIST" -v OUT="$tmp_report" '
BEGIN {
    while ((getline line < KEYS) > 0) {
        if (line == "") continue
        split(line, kv, "\t")
        nk++
        rowno[nk] = kv[1]
        key[nk] = kv[2]
        allow[kv[2]] = 1
    }
    close(KEYS)
}
{
    seen[$3] = 1
    if (!($3 in allow)) {
        unexcused++
        printf "FAIL %s:%s -- a manifest entry is called unportable here, and no allowlist row excuses it\n     %s\n", $1, $2, $3 >> OUT
    }
}
END {
    for (i = 1; i <= nk; i++) {
        if (!(key[i] in seen)) {
            stale++
            printf "FAIL %s row %d is STALE -- its key matches no line in the tree:\n     %s\n", ALLOWFILE, rowno[i], key[i] >> OUT
        }
    }
    printf "%d %d %d\n", unexcused + 0, stale + 0, nk + 0
}
' "$tmp_hits")

unexcused=$(printf '%s' "$xref" | cut -d' ' -f1)
stale=$(printf '%s' "$xref" | cut -d' ' -f2)
valid_keys=$(printf '%s' "$xref" | cut -d' ' -f3)
failures=$((unexcused + stale))
checked=$((hits + valid_keys))

# ASSERTION 3: the allowlist's own rows are well formed.
if [ -s "$allow_bad" ]; then
    while IFS= read -r bad; do
        checked=$((checked + 1))
        failures=$((failures + 1))
        printf 'FAIL %s %s\n' "$ALLOWLIST" "$bad" >> "$tmp_report"
    done < "$allow_bad"
fi

echo "portable_claims_entries=$entry_count"
echo "portable_claims_files=$file_count"
echo "portable_claims_hits=$hits"
echo "portable_claims_allow_rows=$allow_rows"
echo "portable_claims_checked=$checked"
echo "portable_claims_failures=$failures"
echo "portable_claims_stale=$stale"

if [ -s "$tmp_report" ]; then cat "$tmp_report"; fi

if [ "$CONTROL" = "1" ]; then
    # A passing control is failures == hits with hits non-zero. `failures == 0` here would mean
    # the matcher found nothing to excuse, which is indistinguishable from a broken matcher.
    if [ "$hits" -lt 1 ]; then
        echo "portable_claims: CONTROL FAILED -- 0 hits, so the matcher never fired"
        exit 1
    fi
    if [ "$failures" != "$hits" ]; then
        echo "portable_claims: CONTROL FAILED -- $failures failures against $hits hits; with no allowlist every hit must fail"
        exit 1
    fi
    echo "portable_claims: CONTROL OK -- $failures/$hits hits failed with the allowlist withheld"
    exit 0
fi

if [ "$failures" -gt 0 ]; then
    echo "portable_claims: FAIL -- $failures of $checked assertions failed"
    exit 1
fi
echo "portable_claims: OK -- $checked assertions over $file_count files and $entry_count manifest entries"
exit 0

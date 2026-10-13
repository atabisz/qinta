#!/usr/bin/env bash
# Print the rendering fixtures through `qinta --printpages` and write a manifest of what came out:
# page count per fixture, then one line per page with its PNG dimensions and SHA-256. CI runs it
# against the MSVC and the MinGW build and scripts/compare_print_pages.py compares the two, which
# is the "rendering harnesses agree under MinGW" gate README's MinGW section names.
#
#   scripts/print-pages-manifest.sh <qinta executable> <output directory>
#
# The executable must sit beside a settings.ini, qinta's portable-mode opt-in, and the script
# refuses otherwise: every qinta launch writes its settings, and without that file the write lands
# in the user's real configuration. CI copies the binary into a scratch folder first.
#
# Manifest lines:
#   pages <fixture> <count>
#   page <fixture> <png name> <width>x<height> <sha256>
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <qinta executable> <output directory>" >&2
    exit 2
fi
exe=$1
out=$2
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [ ! -f "$exe" ]; then
    echo "print-pages: no executable at $exe" >&2
    exit 2
fi
if [ ! -f "$(dirname "$exe")/settings.ini" ]; then
    echo "print-pages: refusing to run $exe without a settings.ini beside it (portable mode);" \
         "it would write the real configuration" >&2
    exit 2
fi

# The four fixtures the printed-page checks have always used, and supsub-probe.md, which is the
# only one with ^sup^ and ~sub~, so the smallest text format has pixels to compare.
fixtures=(
    README.md
    tests/fixtures/markdown-regression-control.md
    tests/fixtures/math-mixed-layout.md
    tests/fixtures/math-inline-stress.md
    tests/fixtures/supsub-probe.md
)

rm -rf "$out"
mkdir -p "$out"
manifest="$out/manifest.txt"
: > "$manifest"

for fixture in "${fixtures[@]}"; do
    # --printpages on a missing document still prints one blank page and exits 0, so the
    # fixture's existence is checked here rather than inferred from a successful run.
    if [ ! -f "$root/$fixture" ]; then
        echo "print-pages: fixture missing: $fixture" >&2
        exit 1
    fi
    name=$(basename "$fixture" .md)
    dir="$out/$name"
    mkdir -p "$dir"
    err=$("$exe" --printpages "$dir" "$root/$fixture" 2>&1 >/dev/null) || {
        echo "print-pages: qinta failed on $fixture: $err" >&2
        exit 1
    }
    reported=$(printf '%s\n' "$err" | sed -n 's/.*printpages: \([0-9][0-9]*\).*/\1/p' | tail -1)
    count=$(find "$dir" -maxdepth 1 -name '*.png' | wc -l | tr -d ' ')
    if [ -z "$reported" ] || [ "$reported" != "$count" ]; then
        echo "print-pages: $fixture: qinta reported '${reported}' pages, $count PNGs written" >&2
        exit 1
    fi
    echo "pages $name $count" >> "$manifest"
    for png in $(cd "$dir" && ls *.png | sort); do
        # Width and height are the IHDR fields, big-endian at bytes 16-23 of any PNG. A command
        # substitution and not `< <(...)`: MSYS2's bash can lack /dev/fd, the reason
        # check-readme-figures.sh stopped using process substitution.
        size=$(od -An -tu1 -j16 -N8 "$dir/$png" |
            awk '{ printf "%dx%d", $1*16777216 + $2*65536 + $3*256 + $4, $5*16777216 + $6*65536 + $7*256 + $8 }')
        # macOS ships shasum, not GNU sha256sum
        if command -v sha256sum >/dev/null; then
            sum=$(sha256sum "$dir/$png" | cut -d' ' -f1)
        else
            sum=$(shasum -a 256 "$dir/$png" | cut -d' ' -f1)
        fi
        echo "page $name $png $size $sum" >> "$manifest"
    done
    echo "print-pages: $fixture: $count pages"
done

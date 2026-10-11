#!/usr/bin/env python3
"""Compare two scripts/print-pages-manifest.sh outputs: the MSVC and the MinGW build's pages.

    python3 scripts/compare_print_pages.py <msvc dir> <mingw dir>

FAILS (exit 1) when the two builds paginate differently -- a fixture missing from either side,
a different page count, or a page of a different size. Those are layout disagreements.

REPORTS, without failing, pages whose pixels differ. The two Windows jobs install different Qt
releases, and glyph rasterisation moves between Qt releases, so byte-identical pages are not
something these two builds can promise. Each differing page gets the share of its pixels that
changed and the largest channel difference, so a reader can tell anti-aliasing from a moved
line. That needs Pillow; without it the hashes are still compared and the pixel columns say so.
"""
import os
import sys


def read_manifest(directory):
    path = os.path.join(directory, "manifest.txt")
    pages, files = {}, {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if parts and parts[0] == "pages":
                pages[parts[1]] = int(parts[2])
            elif parts and parts[0] == "page":
                files[(parts[1], parts[2])] = (parts[3], parts[4])
    return pages, files


def pixel_difference(a_path, b_path):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    with Image.open(a_path) as a, Image.open(b_path) as b:
        diff = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
        # A pixel changed if ANY channel did: the per-pixel maximum over the three bands is zero
        # only where all three are, so the histogram's zero bucket counts the unchanged pixels.
        r, g, b_band = diff.split()
        peak = ImageChops.lighter(ImageChops.lighter(r, g), b_band)
        changed = diff.width * diff.height - peak.histogram()[0]
        largest = max(high for _, high in diff.getextrema())
        return changed / (diff.width * diff.height), largest


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    a_dir, b_dir = argv[1], argv[2]
    a_pages, a_files = read_manifest(a_dir)
    b_pages, b_files = read_manifest(b_dir)
    if not a_pages or not b_pages:
        print("compare: a manifest lists no fixtures, so there is nothing to compare", file=sys.stderr)
        return 1

    failures = []
    rows = ["| Fixture | MSVC pages | MinGW pages | Identical pages | Largest pixel share changed |",
            "|---|---|---|---|---|"]
    for fixture in sorted(set(a_pages) | set(b_pages)):
        a_count, b_count = a_pages.get(fixture), b_pages.get(fixture)
        if a_count is None or b_count is None or a_count != b_count:
            failures.append(f"{fixture}: MSVC {a_count} pages, MinGW {b_count}")
            rows.append(f"| {fixture} | {a_count} | {b_count} | - | - |")
            continue
        identical, worst = 0, 0.0
        names = sorted(name for (fx, name) in a_files if fx == fixture)
        for name in names:
            a_size, a_sum = a_files[(fixture, name)]
            b_entry = b_files.get((fixture, name))
            if b_entry is None:
                failures.append(f"{fixture}/{name}: missing from the MinGW pages")
                continue
            b_size, b_sum = b_entry
            if a_size != b_size:
                failures.append(f"{fixture}/{name}: MSVC {a_size}, MinGW {b_size}")
                continue
            if a_sum == b_sum:
                identical += 1
                continue
            measured = pixel_difference(os.path.join(a_dir, fixture, name),
                                        os.path.join(b_dir, fixture, name))
            if measured is None:
                worst = None
            else:
                share, largest = measured
                print(f"{fixture}/{name}: {share:.4%} of pixels differ, largest channel delta {largest}")
                if worst is not None:
                    worst = max(worst, share)
        worst_text = "Pillow not installed" if worst is None else f"{worst:.4%}"
        rows.append(f"| {fixture} | {a_count} | {b_count} | {identical} of {a_count} | {worst_text} |")

    table = "\n".join(rows)
    print(table)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("## Printed pages, MSVC against MinGW\n\n" + table + "\n")
    for failure in failures:
        print("FAIL: " + failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

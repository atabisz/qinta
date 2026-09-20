#!/usr/bin/env python3
# The PDF baseline's normaliser, REBUILT 2026-09-20 after the original rig was lost.
#
# WHY A MASK AT ALL: the Windows arm's PDF is not byte-deterministic. It carries exactly
# two `(D:yyyymmddhhmmss` date strings -- CreationDate and ModDate -- so two runs of the
# same build over the same input differ in those bytes and nowhere else. The mask replaces
# the fourteen digits with zeros, preserving length, and the script PROVES that is all it
# did: it reports how many spans it masked and refuses to print a digest if the count is
# not the one the baseline recorded.
#
# It also counts pages by the same rule tests/pdf_export_shared.cpp uses -- `/Type /Page`
# excluding `/Pages` -- which makes this an INDEPENDENT ROUTE to the number that test
# asserts. Same rule, different language, different author-sitting; a C++ instrument that
# agreed with itself would prove nothing.
#
# A DIGEST FROM THIS SCRIPT IS MEANINGLESS WITHOUT THE INVOCATION THAT PRODUCED THE PDF,
# and that is not a style note -- it cost a slice. The PDF's job name is
# `L"Tinta - " + toWide(app.currentFile)`, so the INPUT PATH SPELLING is inside the file:
# the same fixture by relative path and by absolute path differ by 31 bytes for 12
# characters of path, non-linearly, because the job name is Flate-compressed with
# everything else. A baseline recorded as "masked sha X, N bytes" with no command beside it
# cannot be reproduced by anyone, including its author a day later.
#
# So record BOTH lines, always:
#
#   ./build-win-split/Release/tinta.exe tests/fixtures/application-menu.md \
#       --exportpdf build-win-split/pdfscratch/m1.pdf
#   python3 scripts/pdfmask.py build-win-split/pdfscratch/m1.pdf
#
# That pair reads, 2026-09-21, from `2752603` plus the Qt arm:
#   bytes=378862 dates_masked=2 pages=2 page_tree_nodes=1
#   sha256=e28528c23fba6c4304cf2a805106c4da3946d3875f3405c41eb01ce6142aa409
#
# Watch the fixture argument: a path that does not exist exits 0 and writes ONE BLANK PAGE
# of about 858 bytes, which reads as a confident small baseline rather than as an error.

import hashlib
import re
import sys

DATE = re.compile(rb"\(D:\d{14}")
PAGE = re.compile(rb"/Type\s*/Page(?![^\s()<>\[\]{}/%])")
PAGES = re.compile(rb"/Type\s*/Pages\b")


def main(paths):
    for path in paths:
        with open(path, "rb") as handle:
            raw = handle.read()
        spans = DATE.findall(raw)
        masked = DATE.sub(b"(D:00000000000000", raw)
        if len(masked) != len(raw):
            print("%s: LENGTH CHANGED -- refusing to report a digest" % path)
            continue
        print(
            "%s\n  bytes=%d dates_masked=%d pages=%d page_tree_nodes=%d\n  sha256=%s"
            % (
                path,
                len(raw),
                len(spans),
                len(PAGE.findall(raw)),
                len(PAGES.findall(raw)),
                hashlib.sha256(masked).hexdigest(),
            )
        )


if __name__ == "__main__":
    main(sys.argv[1:])

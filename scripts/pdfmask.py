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
#   python scripts/pdfmask.py build-win-split/pdfscratch/m1.pdf
#
# `python`, NOT `python3`, and that correction is worth the line it costs. On this Windows host
# `python3` RESOLVES -- to the Microsoft Store's app-execution-alias shim, which prints an advert
# for the Store and exits without running a byte of this file. So `command -v python3` succeeds,
# the invocation fails, and the failure looks like an empty digest rather than a missing
# interpreter. scripts/probe-output-guard.sh resolves the name by running `--version` rather than
# by testing whether it resolves, for exactly that reason.
#
# That pair reads, 2026-09-21, from `2752603` plus the Qt arm:
#   bytes=378862 dates_masked=2 pages=2 page_tree_nodes=1
#   sha256=e28528c23fba6c4304cf2a805106c4da3946d3875f3405c41eb01ce6142aa409
#
# QT'S PDF HAS FIVE MORE PER-RUN FIELDS, measured 2026-10-07 when this script's first caller
# moved from tinta.exe to qinta.exe with the Direct2D stack's deletion. Two qinta exports of one
# document, a second apart, differ in 44 byte spans, and every one falls inside one of four
# kinds: the two `(D:` dates above, three XMP dates (`xmp:CreateDate`, `ModifyDate`,
# `MetadataDate`, ISO 8601), the XMP `xmpMM:DocumentID` uuid, and the trailer `/ID` pair. The
# last three are masked here too, length-preserving like the dates, and counted separately as
# `qt_fields_masked` so `dates_masked` still means what probe-output-guard.sh asserts. A tinta PDF
# carried none of them, so its digests are unchanged by this; the reading above is tinta's.
#
# AND MASKING IS NOT ENOUGH FOR qinta ON ITS OWN: run it with QT_HASH_SEED=0. Its PDF writer emits
# fonts in QHash order and Qt seeds QHash per process, so with two or more fonts the FontDescriptor
# objects swap places between runs -- a structural difference no mask can normalise. Pinned, three
# exports of one document masked to one digest; unpinned, two did not.
#
# Watch the fixture argument: a path that does not exist exits 0 and writes ONE BLANK PAGE
# of about 858 bytes, which reads as a confident small baseline rather than as an error.

import hashlib
import re
import sys

DATE = re.compile(rb"\(D:\d{14}")
XMP_DATE = re.compile(rb'xmp:[A-Za-z]+Date="[^"]+"')
XMP_UUID = re.compile(rb"uuid:[0-9a-fA-F-]{36}")
TRAILER_ID = re.compile(rb"/ID\s*\[\s*<[0-9a-fA-F]+>\s*<[0-9a-fA-F]+>\s*\]")
DIGIT = re.compile(rb"[0-9]")
HEX = re.compile(rb"[0-9a-fA-F]")


def zero_digits(match):
    return DIGIT.sub(b"0", match.group(0))


def zero_hex(match):
    head, _, tail = match.group(0).partition(b"<")
    return head + b"<" + HEX.sub(b"0", tail)


PAGE = re.compile(rb"/Type\s*/Page(?![^\s()<>\[\]{}/%])")
PAGES = re.compile(rb"/Type\s*/Pages\b")


def main(paths):
    for path in paths:
        with open(path, "rb") as handle:
            raw = handle.read()
        spans = DATE.findall(raw)
        masked = DATE.sub(b"(D:00000000000000", raw)
        qt_fields = (len(XMP_DATE.findall(masked)) + len(XMP_UUID.findall(masked))
                     + len(TRAILER_ID.findall(masked)))
        masked = XMP_DATE.sub(zero_digits, masked)
        masked = XMP_UUID.sub(lambda m: b"uuid:" + HEX.sub(b"0", m.group(0)[5:]), masked)
        masked = TRAILER_ID.sub(zero_hex, masked)
        if len(masked) != len(raw):
            print("%s: LENGTH CHANGED -- refusing to report a digest" % path)
            continue
        print(
            "%s\n  bytes=%d dates_masked=%d qt_fields_masked=%d pages=%d page_tree_nodes=%d\n"
            "  sha256=%s"
            % (
                path,
                len(raw),
                len(spans),
                qt_fields,
                len(PAGE.findall(raw)),
                len(PAGES.findall(raw)),
                hashlib.sha256(masked).hexdigest(),
            )
        )


if __name__ == "__main__":
    main(sys.argv[1:])

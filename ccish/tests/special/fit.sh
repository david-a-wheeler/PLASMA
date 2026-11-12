#!/bin/sh
# Check that each of ccish's programs fits on a 64K Apple II, in the
# form that runs there (tools/measure.py's APPLE_LINK: XLOAD images):
# its "running, needs N of FREE" line in doc/measure.txt, which make
# test regenerates first, or in the report given as the argument, and
# its "loading, needs N of FREE" line, where there is one (XLOAD's).
# Run from the ccish directory; prints each program that doesn't fit
# and exits non-zero if any doesn't. The frame stack there is often a
# lower bound, so a small margin is less safe than it looks.

set -u
report=${1:-doc/measure.txt}
awk -v report="$report" '
/^[a-z]+: [0-9]+ files, and / { prog = $1; sub(":", "", prog) }
/^    (running|loading), needs [0-9]+ of [0-9]+:/ {
    if ($1 == "running,")
        n++
    if ($3 + 0 > $5 + 0) {
        printf "fit.sh: %s needs %d bytes %s, over the %d free on a 64K" \
               " Apple II by %d\n", prog, $3, $1 == "running," ? \
               "running" : "loading", $5, $3 - $5
        bad = 1
    }
}
END {
    if (n != 4) {
        printf "fit.sh: %d programs measured in %s, not 4\n", n, report
        bad = 1
    }
    exit bad
}' "$report"

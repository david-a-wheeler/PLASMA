#!/bin/sh
# imgrun.sh NAME: the img test of run test tests/run/NAME.c (the
# Makefile): compile it as an XLOAD image (ccish -x) into tests/out/img,
# run it through XLOAD under plvm, and check its stdout and exit status
# as the run test does (tests/run/NAME.expected and .status). plvm's
# LOOKUPSYM traces, from XLOAD's imports (doc/plasma-requests.md), are
# dropped. A run test whose source has a line starting "// img-skip:"
# (and why) doesn't apply to an image, and passes.
. "$(dirname "$0")/lib.sh"
n=$1
d=$OUT/img
src=tests/run/$n.c
if grep -q '^// img-skip:' "$src"; then
    exit 0
fi
./ccish -x -o "$d/$n" "$src" 2>/dev/null || { echo "img $n: no image"; exit 1; }
(cd "$d" && "$CCRUN" -t 10 xload "$n" >"$n.raw" 2>&1)
got=$?
tr -d '\r' <"$d/$n.raw" | sed '/^LOOKUPSYM: /d' >"$d/$n.actual"
diff -u "tests/run/$n.expected" "$d/$n.actual" || exit 1
want=$(cat "tests/run/$n.status" 2>/dev/null || echo 0)
if [ "$got" != "$want" ]; then
    echo "img $n: exit status $got, expected $want"
    exit 1
fi

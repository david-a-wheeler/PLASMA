#!/bin/sh
# Check ccgen's op-indexed tables against ir.h's op numbers
# (tests/spec/tables.c): built on the host, with ccgen's sources less
# its main (ccgen.c) and the two files tables.c includes.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out/tables
rm -rf "$OUT"
mkdir -p "$OUT"
${CC:-cc} -Wall -Wextra -O0 -Isrc -o "$OUT/tables" tests/spec/tables.c \
    src/diag.c src/bio.c src/nam.c src/irloop.c src/peep.c src/emit.c \
    src/helper.c || exit 1
"$OUT/tables" || exit 1
rm -rf "$OUT"

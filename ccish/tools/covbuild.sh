#!/bin/sh
# Build the host's ccish and its four passes, instrumented for gcov,
# for the coverage targets (doc/coverage.md): each program's sources
# into $OBJ/<prog>/, the .gcno beside each .o, the program in
# $BIN/<prog>, and ccish.msg beside the programs (diag looks for it
# beside the program, src/diag.c). When a program runs, its .gcda
# counters go beside the .gcno, whatever the working directory.
#
# The Makefile gives CC, CFLAGS (with -fprofile-arcs
# -ftest-coverage added), INC (the library's include directory),
# BIN and OBJ, absolute, and SRC_<prog> for each program. The .gcda
# are removed first: a rebuild invalidates them. CC must be gcc, as
# gcov reads its data.
set -eu
for v in CC CFLAGS INC BIN OBJ; do
    eval "val=\${$v:-}"
    [ -n "$val" ] || { echo "covbuild: $v from the Makefile"; exit 2; }
done
# The preprocessor define of the default include directory, a C string
# literal (src/pp.c), so its quotes are part of the argument.
INC_FLAG=-DCCISH_INC="\"$INC\""
rm -f "$OBJ"/*/*.gcda
mkdir -p "$BIN"
for p in ccish ccpp cccomp ccgen cclink; do
    eval "list=\${SRC_$p:-}"
    [ -n "$list" ] || { echo "covbuild: SRC_$p from the Makefile"; exit 2; }
    mkdir -p "$OBJ/$p"
    for s in $list; do
        n=${s##*/}
        "$CC" $CFLAGS "$INC_FLAG" -c "$s" -o "$OBJ/$p/${n%.c}.o"
    done
    "$CC" -fprofile-arcs -o "$BIN/$p" "$OBJ/$p"/*.o
done
cp ccish.msg "$BIN/"

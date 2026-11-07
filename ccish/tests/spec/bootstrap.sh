#!/bin/sh
# The compiler bootstrap test.
#
# The compiler compiles itself, twice, under the VM, and the two
# results must be byte-for-byte the same module:
#
#   stage 1  the seed, the passes the regular Makefile rules built
#            (this source's host build), compiles the compiler's
#            source into a module for the VM, s1/cc;
#   stage 2  s1/cc, run under the VM ($BOOTSTRAP_VM), compiles the
#            same source into s2/cc;
#   stage 3  s2/cc does it again into s3/cc;
#   test     s2/cc and s3/cc are byte-for-byte identical.
#
# It is run by `make bootstrap` (or `make full-test`), not by the
# regular `make test` suite. Each stage keeps its working directory,
# with its IR and machine-op dumps, for diagnosis (doc/bootstrap-test.md).
#
# The stage command lines must fit the VM's 128-byte command-line
# buffer, so in a stage directory the sources are one-letter names,
# a.c through x.c, in link order.

. "$(dirname "$0")/../lib.sh"
fail() { echo "bootstrap: $*"; exit 1; }

[ -n "$BOOTSTRAP_VM" ] || fail "BOOTSTRAP_VM is not set (run via make)"
[ -x ./ccish ] || fail "the stage-1 compiler (./ccish) is not built; run make"

BOOT=bootstrap
rm -rf "$BOOT"
mkdir -p "$BOOT" || fail "cannot create $BOOT"

# The compiler's sources, in link order, and the one-letter name each
# gets in a stage directory (see above).
srcs="main diag bio ppname pplex ppmac ppif ppout pp nam tokin name type parse pexpr pstmt ptop irout irin lower peep emit helper link vmsys"
names="a b c d e f g h i j k l m n o p q r s t u v w x y"

files=""
set -- $srcs
for L in $names; do
  [ $# -gt 0 ] || fail "more source names than sources"
  S=$1
  shift
  [ -f "src/$S.c" ] || fail "missing source src/$S.c"
  for s in 1 2 3; do
    mkdir -p "$BOOT/s$s" || fail "cannot create $BOOT/s$s"
    cp "src/$S.c" "$BOOT/s$s/$L.c" || fail "cannot copy $S.c"
  done
  files="$files $L.c"
done
[ $# -eq 0 ] || fail "more sources than names"
# The headers a stage compiles against, flat beside the sources: the
# project ones (quoted #include, looked for beside the including file)
# and the std ones for a build by ccish (src/std).
for s in 1 2 3; do
  cp src/*.h "$BOOT/s$s/" || fail "cannot copy headers to $BOOT/s$s"
  cp src/std/*.h "$BOOT/s$s/" || fail "cannot copy std headers to $BOOT/s$s"
done

# Stage 1: the seed (this source's host build). The host ccish has the
# real argv, so paths may
# be long; -k keeps its intermediate files too.
echo "bootstrap: stage 1 (the seed, ./ccish)"
( cd "$BOOT/s1" && timeout 600 ../../ccish -DCCISH_SMALL -k -o cc $files ) \
  || fail "stage 1 failed (see above)"

# Stages 2 and 3: the previous stage's module under the VM, with the
# IR dump (-i), the machine-op dump after the peephole stage (-d), and
# the output module named cc (lowercase, no extension, for plvm).
for s in 2 3; do
  p=$((s-1))
  echo "bootstrap: stage $s ($BOOTSTRAP_VM)"
  ( cd "$BOOT/s$s" && timeout 3600 "$BOOTSTRAP_VM" "../s$p/cc" \
        -DSMALL -i -d -o cc $files ) \
    || fail "stage $s failed (see above)"
done

# The test.
if cmp -s "$BOOT/s2/cc" "$BOOT/s3/cc"; then
  echo "bootstrap: stage 2 and stage 3 are byte-for-byte identical"
else
  fail "stage 2 and stage 3 differ (kept in $BOOT/s2/cc and $BOOT/s3/cc)"
fi

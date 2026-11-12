#!/bin/sh
# ccish's own four programs as XLOAD images (doc/xload.md, plan step
# 8): linked from tools/measure.py's objects (the small build's, as the
# Apple II runs them), both as REL modules and as images, they must
# give the same results, byte for byte. Under plvm, each pass compiles
# a few of ccish's own sources (the largest among them) and the hello
# test, as a REL module (rel/) and as an image run through XLOAD
# (img/); every file each writes must be the same, and so must the
# module ccrel links for hello.
. "$(dirname "$0")/../lib.sh"
D=$OUT/xlself
W=$D/w
bad=0
rm -rf "$D"
mkdir -p "$W/src" "$W/sys" "$D/rel" "$D/img" "$D/obj"
cp -r lib/include "$W/sys/ccinc"
cp ccish.msg "$OUT/xload/xload" "$W/"
for f in name bio parse link peep; do
    cp "src/$f.c" "$W/src/"
done
cp tests/run/hello.c src/*.h "$W/src/"

# Each program: the REL module measure.py linked, and an image of the
# same objects (named bare in a list file, as ccrel's @file takes them).
for p in ccpp cccomp ccgen ccrel; do
    if [ ! -f "tools/out/$p.objs" ]; then
        echo "FAIL xload_self: no tools/out/$p.objs (run tools/measure.py)"
        exit 1
    fi
    cp "tools/out/$p" "$D/rel/$p"
    while read -r o; do
        cp "$o.obj" "$o.syms" "$D/obj/"
        basename "$o"
    done <"tools/out/$p.objs" >"$D/obj/$p.list"
    (cd "$D/obj" && ../../../../ccrel-small -x -o "../img/$p" "@$p.list") \
        || { echo "FAIL xload_self: $p doesn't link as an image"; exit 1; }
done

# Run pass $2 with args $3... in $W, as a REL module (rel) or through
# XLOAD (img); fail on a nonzero status.
pass() {
    k=$1
    shift
    if [ "$k" = img ]; then
        (cd "$W" && "$CCRUN" -t 300 xload "$@" >>log 2>&1)
    else
        (cd "$W" && "$CCRUN" -t 300 "$@" >>log 2>&1)
    fi || { echo "FAIL xload_self: $k: $* failed"; sed 's/^/    /' "$W/log"
            bad=1; }
}
# In the same directory, since the name file records the headers'
# paths: the REL programs first, their output moved to $D/out, then the
# images, under the same names.
OUTS="name bio parse link peep hello"
for k in rel img; do
    cp "$D/$k/ccpp" "$D/$k/cccomp" "$D/$k/ccgen" "$D/$k/ccrel" "$W/"
    for f in $OUTS; do
        pass $k ccpp -DCCISH_SMALL -k -o $f src/$f.c
        pass $k cccomp -k $f
        pass $k ccgen -k $f
    done
    pass $k ccrel hello
    if [ $k = rel ]; then
        mkdir -p "$D/out"
        for f in $OUTS; do
            mv "$W/$f.i" "$W/$f.nam" "$W/$f.ir" "$W/$f.obj" "$W/$f.syms" "$D/out/"
        done
        mv "$W/hello" "$D/out/"
    fi
done
for f in $OUTS; do
    for s in .i .nam .ir .obj .syms; do
        cmp -s "$D/out/$f$s" "$W/$f$s" \
            || { echo "FAIL xload_self: $f$s differs"; bad=1; }
    done
done
cmp -s "$D/out/hello" "$W/hello" \
    || { echo "FAIL xload_self: ccrel's hello differs"; bad=1; }
[ $bad = 0 ] && rm -rf "$D"
exit $bad

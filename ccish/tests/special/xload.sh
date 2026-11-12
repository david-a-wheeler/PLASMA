#!/bin/sh
# XLOAD images (ccrel -x; doc/xload.md, section 3): until XLOAD
# exists to run them, tools/xlcheck.py checks each against the REL
# module of the same link. For a few run tests and for ccish's own
# four programs (tools/measure.py's objects, the small build's), both
# ccrels write the same image, and it describes the same program as the
# module. And a link with no main, or with too many functions, can't
# be an image.
. "$(dirname "$0")/../lib.sh"
D=$OUT/xload
bad=0
rm -rf "$D"
mkdir -p "$D"

# check NAME: ccrel and ccrel-small write the same image from
# $D/NAME's objects (in list file $D/NAME.objs), and it matches the
# module.
check() {
    if ! (cd "$D" && ../../../ccrel -o "$1" "@$1.objs" \
              && ../../../ccrel -x -o "$1.xl" "@$1.objs" \
              && ../../../ccrel-small -x -o "$1.xls" "@$1.objs") \
            2>"$D/$1.err"; then
        echo "FAIL xload: $1 doesn't link:"; sed 's/^/    /' "$D/$1.err"
        bad=1
    elif ! cmp -s "$D/$1.xl" "$D/$1.xls"; then
        echo "FAIL xload: $1: ccrel-small's image differs from ccrel's"
        bad=1
    elif ! python3 tools/xlcheck.py "$D/$1" "$D/$1.xl" >"$D/$1.err"; then
        echo "FAIL xload: $1:"; sed 's/^/    /' "$D/$1.err"
        bad=1
    fi
}

# Run tests with data relocations, function pointers, addresses with
# offsets, and imports.
for t in hello global_init pointers addr_const c_names; do
    cc_obj "" "$D/$t" "tests/run/$t.c"
    echo "$t" >"$D/$t.objs"
    check "$t"
done

# ccish's own programs: every function, helper, moved global and
# import of a real link (FILEIO's too). The objects are measure.py's,
# named in its lists by absolute path; the list file takes them bare.
for p in ccpp cccomp ccgen ccrel; do
    if [ ! -f "tools/out/$p.objs" ]; then
        echo "FAIL xload: no tools/out/$p.objs (run tools/measure.py)"
        bad=1
        continue
    fi
    while read -r o; do
        cp "$o.obj" "$o.syms" "$D/"
        basename "$o"
    done <"tools/out/$p.objs" >"$D/$p.objs"
    check "$p"
done

# No main, no image.
echo 'int f(void) { return 1; }' >"$D/nomain.c"
cc_obj "" "$D/nomain" "$D/nomain.c"
if ./ccrel -x -o "$D/nomain.xl" "$D/nomain" 2>"$D/nomain.err"; then
    echo "FAIL xload: an image linked without main"; bad=1
elif ! grep -qF "an XLOAD image needs main" "$D/nomain.err"; then
    echo "FAIL xload: wanted 'an XLOAD image needs main', got:"
    sed 's/^/    /' "$D/nomain.err"; bad=1
fi

# More functions than an image's def table holds (MAX_DEFS, 320, with
# init): two objects of 160 static functions and a wrapper each.
gen_funcs() {
    i=$2
    while [ "$i" -le "$3" ]; do
        echo "static int f$i(void) { return $i; }"
        i=$((i + 1))
    done >"$1"
    echo "int $4(void) { return f$2() + f$3(); }" >>"$1"
}
gen_funcs "$D/many_a.c" 1 160 a
gen_funcs "$D/many_b.c" 161 320 b
echo 'int a(void); int main() { return a() + b(); }' >>"$D/many_b.c"
cc_obj "" "$D/many_a" "$D/many_a.c"
cc_obj "" "$D/many_b" "$D/many_b.c"
if ./ccrel -x -o "$D/many.xl" "$D/many_a" "$D/many_b" 2>"$D/many.err"; then
    echo "FAIL xload: an image linked with 324 functions (init's too)"; bad=1
elif ! grep -qF "too many functions for an XLOAD image" "$D/many.err"; then
    echo "FAIL xload: wanted 'too many functions for an XLOAD image', got:"
    sed 's/^/    /' "$D/many.err"; bad=1
fi
[ $bad = 0 ] && rm -rf "$D"
exit $bad

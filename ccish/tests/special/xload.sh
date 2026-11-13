#!/bin/sh
# XLOAD (doc/xload.md) and its images (ccish -x, ccrel -x), under plvm.
# The run tests' images run through XLOAD in the img tests (the
# Makefile); this checks the rest:
#   - XLOAD's test program (tests/xload/xltest.c), which holds what an
#     image can, runs through XLOAD -V with its output and status, and
#     XLOAD's diagnostics agree with tools/xlinfo.py's reading of the
#     image (written apart from ccrel and XLOAD) and with themselves
#     (each part where the sizes put it, the second part clear of the
#     program but for its zero region);
#   - XLOAD's errors: no program, no such file, a REL module instead of
#     an image, a missing module, a missing import;
#   - ccrel and ccrel-small write the same images;
#   - a link with no main, or with too many functions, isn't an image.
. "$(dirname "$0")/../lib.sh"
D=$OUT/xltest
bad=0
rm -rf "$D"
mkdir -p "$D"
cp "$OUT/xload/xload" "$OUT/xload/xload2" "$D/"

# Run XLOAD in $D with args $@: its output, CRs and plvm's LOOKUPSYM
# traces out, in $D/out, and its status in $st.
xl() {
    (cd "$D" && "$CCRUN" -t 10 xload "$@" >out.raw 2>&1)
    st=$?
    tr -d '\r' <"$D/out.raw" | sed '/^LOOKUPSYM: /d' >"$D/out"
}

# The test program, and its diagnostics.
./ccish -x -o "$D/xltest" tests/xload/xltest.c "$OUT/lib/start"
xl -V xltest a bc
grep -v '^XLOAD: ' "$D/out" >"$D/prog"
if [ $st != 7 ] || ! diff -u tests/xload/xltest.expected "$D/prog"; then
    echo "FAIL xload: xltest's output or status ($st, not 7)"; bad=1
fi
python3 tools/xlinfo.py "$D/xltest" >"$D/info" || bad=1
sed -n 's/^XLOAD: \(data .*\)/\1/p; s/^XLOAD: \(fixups .*\)/\1/p' "$D/out" \
    >"$D/diag"
sed -n '1,2p' "$D/info" | diff -u - "$D/diag" \
    || { echo "FAIL xload: -V's sizes and fixups differ from xlinfo's"; bad=1; }
sed -n 's/^XLOAD: import \([A-Z0-9_]*\) .*/import \1/p' "$D/out" >"$D/imps"
grep '^import ' "$D/info" | diff -u - "$D/imps" \
    || { echo "FAIL xload: -V's imports differ from xlinfo's"; bad=1; }
grep -qx 'XLOAD: VM plvm, entries JSR \$FF08' "$D/out" \
    || { echo "FAIL xload: -V didn't find plvm"; bad=1; }
# The places: defs + 5 per function is the body, + data the zero region,
# + zero the code, + code the end; init the last def entry; the second
# part above the end, or inside the zero region.
awk -v info="$D/info" '
    BEGIN {
        getline l < info
        split(l, w, /[ ,]+/)
        d = w[2]; z = w[4]; c = w[6]; n = w[8]
    }
    function hex(s,   i, v) {
        v = 0
        for (i = 2; i <= length(s); i++)
            v = v * 16 + index("0123456789ABCDEF", substr(s, i, 1)) - 1
        return v
    }
    /^XLOAD: defs / {
        gsub(/[,;]/, "")
        defs = hex($3); body = hex($5); zero = hex($7); code = hex($9)
        end = hex($11); xl2 = hex($13)
    }
    /^XLOAD: init / { gsub(/[,;]/, ""); init = hex($3) }
    END {
        ok = body == defs + 5 * n && zero == body + d && code == zero + z \
             && end == code + c && init == defs + 5 * (n - 1) \
             && (xl2 >= end || (xl2 >= zero && xl2 < code))
        exit !ok
    }' "$D/out" || { echo "FAIL xload: -V's places don't add up:"
                     grep '^XLOAD: \(defs\|init\)' "$D/out"; bad=1; }

# The hand-off rests on the first part's main (4.5): no frame (no
# ENTER), and the capture routine called with nothing on the
# evaluation stack: its address pushed (LA) right after a branch or at
# the start, then ICAL.
./ccish -v -o "$D/xlfirst" xload/xload.c >"$D/xlfirst.lst"
sed -n '/^main:$/,/^[^ ]/p' "$D/xlfirst.lst" | awk '
    / ENTER / { bad = 1 }
    / ICAL/ && prev !~ / LA / { bad = 1 }
    / ICAL/ { icals++ }
    { prev = $0 }
    END { exit bad || icals != 1 }' \
    || { echo "FAIL xload: the first part'\''s main has a frame, or its capture call isn'\''t LA ICAL"
         bad=1; }

# The errors.
want() {    # want STATUS TEXT: XLOAD's last run
    if [ $st != "$1" ] || ! grep -qF "$2" "$D/out"; then
        echo "FAIL xload: wanted status $1 and '$2', got $st:"
        sed 's/^/    /' "$D/out"; bad=1
    fi
}
xl
want 2 "usage: +XLOAD"
xl nosuch
want 1 "XLOAD: can't open nosuch"
./ccish -o "$D/rel" tests/run/hello.c
xl rel
want 1 "XLOAD: not an XLOAD image, version 4"
cat >"$D/nomod.c" <<'EOF'
#pragma plasma import NOSUCH
int nosuch(void);
int main(void) { return nosuch(); }
EOF
./ccish -x -o "$D/nomod" "$D/nomod.c"
xl nomod
want 1 "XLOAD: needs the module NOSUCH"
cat >"$D/noimp.c" <<'EOF'
#pragma plasma import FILEIO
int bogus(void);
int main(void) { return bogus(); }
EOF
./ccish -x -o "$D/noimp" "$D/noimp.c"
xl noimp
want 1 "XLOAD: can't import BOGUS"

# 64,000 bytes, over the 60K or so free under plvm (its frame stack
# starts near $FF00): 32,000 of data and 32,000 of zero region (one
# object's data is at most 32,767 bytes, and so is its zero region).
cat >"$D/huge.c" <<'EOF'
char full[32000] = {1};
char huge[32000];
int main(void) { return full[1] + huge[1]; }
EOF
./ccish -x -o "$D/huge" "$D/huge.c"
xl huge
want 1 "XLOAD: not enough memory"

# ccrel and ccrel-small write the same images.
for t in hello global_init pointers addr_const c_names; do
    cc_obj "" "$D/$t" "tests/run/$t.c"
    if ! (cd "$D" && ../../../ccrel -x -o "$t.xl" "$t" \
              && ../../../ccrel-small -x -o "$t.xls" "$t") 2>"$D/$t.err" \
       || ! cmp -s "$D/$t.xl" "$D/$t.xls"; then
        echo "FAIL xload: $t: ccrel-small's image differs from ccrel's"
        sed 's/^/    /' "$D/$t.err"; bad=1
    fi
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

# More functions than an image's table holds (MAX_DEFS, 320, with
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

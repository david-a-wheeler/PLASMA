#!/bin/sh
# The back-end checks: ccgen -i prints the IR as it reads it (IR.md),
# ccgen finds a damaged IR file or name file (OBJECT.md, CCPP.md), and a
# back-end error names the source file and the line where the function
# starts.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT"

# ccgen -i prints the IR as it reads it (IR.md; written by hand): the
# function's line record, then each op. main's body starts at line 2;
# puti returns nothing; falling off main's end returns 0, which the
# peephole stage drops later.
printf 'int main()\n{\n  __puti(7);\n  return 0;\n}\n' >"$OUT/ir_dump.c"
printf '%s\n' '; line 2' 'IR FUNC     main' 'IR CONST    7' \
    'IR CALL     __puti nargs=1' 'IR CONST    0' 'IR RETURN  ' \
    'IR CONST    0' 'IR RETURN  ' 'IR ENDFUNC  frame=0' >"$OUT/ir_dump.want"
if cc_ir "$OUT/ir_dump" "$OUT/ir_dump.c" \
   && ./ccgen -i "$OUT/ir_dump" >"$OUT/ir_dump.got" \
   && cmp -s "$OUT/ir_dump.want" "$OUT/ir_dump.got"; then
    :
else
    echo "FAIL ccgen -i: wanted, then got:"
    sed 's/^/    /' "$OUT/ir_dump.want"
    echo "    ---"
    sed 's/^/    /' "$OUT/ir_dump.got"
    bad=1
fi
rm -f "$OUT/ir_dump.obj" "$OUT/ir_dump.syms"

# ccgen finds a damaged IR file (OBJECT.md, "What ccgen checks"): cut
# short, a wrong version, an unknown tag, a wrong SWAP length. It writes
# no stream files, and keeps the IR file (and the name file).
ir_damaged_test() {
    if ./ccgen "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccgen: accepted an IR file $2"; bad=1
    elif ! grep -qF "damaged: $OUT/$1.ir" "$OUT/$1.err"; then
        echo "FAIL ccgen: wrong message for an IR file $2:"
        sed 's/^/    /' "$OUT/$1.err"; bad=1
    elif [ -e "$OUT/$1.obj" ] || [ -e "$OUT/$1.syms" ] \
         || [ ! -e "$OUT/$1.ir" ]; then
        echo "FAIL ccgen: an IR file $2 left stream files, or was removed"; bad=1
    fi
    rm -f "$OUT/$1.ir" "$OUT/$1.nam"
}
cc_ir "$OUT/ir_cut" tests/run/global_init.c
size=$(wc -c < "$OUT/ir_cut.ir")
head -c $((size - 3)) "$OUT/ir_cut.ir" > "$OUT/ir_cut.tmp"
mv "$OUT/ir_cut.tmp" "$OUT/ir_cut.ir"
ir_damaged_test ir_cut "cut short"
cc_ir "$OUT/ir_version" tests/run/hello.c
printf '\377' | dd of="$OUT/ir_version.ir" bs=1 seek=3 conv=notrunc \
    2>/dev/null
ir_damaged_test ir_version "with a wrong version"
cc_ir "$OUT/ir_tag" tests/run/hello.c
printf 'Q' >> "$OUT/ir_tag.ir"
ir_damaged_test ir_tag "with something after the summary"
# The full build's ccgen checks each block it moves (CCISH_CHECKS): a
# SWAP whose length is wrong is a damaged file, not wrong code. The SWAP
# is the first $B2 byte after the 8-byte header; its length is the byte
# after it. The small build doesn't check.
printf 'int g;\nint *f(void) { return &g; }\nint main() { *f() = 5; return 0; }\n' \
    >"$OUT/ir_badswap.c"
cc_ir "$OUT/ir_badswap" "$OUT/ir_badswap.c"
at=$(od -An -v -tu1 "$OUT/ir_badswap.ir" \
     | awk '{ for (i = 1; i <= NF; i++) {
                if ($i == 178 && n >= 8) { print n; exit }
                n++ } }')
printf '\002' | dd of="$OUT/ir_badswap.ir" bs=1 seek=$((at + 1)) \
    conv=notrunc 2>/dev/null
ir_damaged_test ir_badswap "with a wrong SWAP length"
# ccgen spells the names from the name file, so it checks it too (CCPP.md,
# "The stamp"): one damaged, and one from another run of ccpp.
gen_nam_test() {
    if ./ccgen "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccgen: accepted a name file $2"; bad=1
    elif ! grep -qF "fatal 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err"; then
        echo "FAIL ccgen: wrong message for a name file $2:"
        sed 's/^/    /' "$OUT/$1.err"; bad=1
    elif [ -e "$OUT/$1.obj" ] || [ ! -e "$OUT/$1.ir" ] \
         || [ ! -e "$OUT/$1.nam" ]; then
        echo "FAIL ccgen: a name file $2 left stream files, or lost an input"; bad=1
    fi
    rm -f "$OUT/$1.ir" "$OUT/$1.nam"
}
cc_ir "$OUT/gen_nam_bad" tests/run/hello.c
sed '/^$/d' "$OUT/gen_nam_bad.nam" >"$OUT/gen_nam_bad.tmp"
mv "$OUT/gen_nam_bad.tmp" "$OUT/gen_nam_bad.nam"
gen_nam_test gen_nam_bad "without its blank line"
cc_ir "$OUT/gen_nam_stale" tests/run/hello.c
cp tests/run/hello.c "$OUT/gen_nam_other.c"
./ccpp -o "$OUT/gen_nam_other" "$OUT/gen_nam_other.c"
mv "$OUT/gen_nam_other.nam" "$OUT/gen_nam_stale.nam"
rm -f "$OUT/gen_nam_other.i"
gen_nam_test gen_nam_stale "from another run of ccpp"
# A back-end error gives the source file and the line where the
# function starts (the IR file's line record): here, line 1.
{
    echo "int main() { int x;"
    i=0
    while [ $i -lt 900 ]; do echo "x = 1000;"; i=$((i + 1)); done
    echo "return 0; }"
} >"$OUT/be_too_large.c"
"$CCISH" -o "$OUT/be_too_large" "$OUT/be_too_large.c" 2>"$OUT/be_too_large.err"
if grep -qF "be_too_large.c:1: error 177: function too large: main" \
        "$OUT/be_too_large.err"; then
    :
else
    echo "FAIL ccgen: a back-end error should give the file and line:"
    sed 's/^/    /' "$OUT/be_too_large.err"; bad=1
fi
exit $bad

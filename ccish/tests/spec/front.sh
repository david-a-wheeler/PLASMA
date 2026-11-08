#!/bin/sh
# The front-end checks: cccomp finds a damaged or stale name file or
# token stream (CCPP.md), the per-function limits fail, cccomp/ccpp leave
# no IR/output on error, and an output name over MAX_PATH is refused.
# The passes-as-programs handoff (ccpp -> cccomp -> ccgen -> ccrel keeps
# and passes on its files) is here too, plus the -k tests.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT"

# cccomp finds a damaged or stale name file (CCPP.md, "What cccomp
# checks"): each change below is made to hello.c's, which must then be
# refused with message 232, leaving no IR file.
nam_damaged_test() {
    if ./cccomp "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL cccomp: accepted a name file $2"; bad=1
    elif ! grep -qF "error 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err" && \
         ! grep -qF "fatal 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err"; then
        echo "FAIL cccomp: wrong message for a name file $2:"
        sed 's/^/    /' "$OUT/$1.err"; bad=1
    elif [ -e "$OUT/$1.ir" ]; then
        echo "FAIL cccomp: a name file $2 left an IR file"; bad=1
    fi
    rm -f "$OUT/$1.i" "$OUT/$1.nam"
}
nam_edit() {
    ./ccpp -o "$OUT/$1" tests/run/hello.c
    sed "$2" "$OUT/$1.nam" >"$OUT/$1.tmp"
    mv "$OUT/$1.tmp" "$OUT/$1.nam"
}
nam_edit nam_noblank '/^$/d'
nam_damaged_test nam_noblank "without its blank line"
nam_edit nam_extra '1a\
extra'
nam_damaged_test nam_extra "with a line added"
nam_edit nam_lost '3d'
nam_damaged_test nam_lost "with a line lost"
nam_edit nam_count 's/^=\([0-9]\) \(....\)/=\1 FFFF/'
nam_damaged_test nam_count "with a wrong count"
nam_edit nam_sum 's/^__putc$/__putd/'
nam_damaged_test nam_sum "with a wrong checksum"
nam_edit nam_after '$a\
x'
nam_damaged_test nam_after "with a line after the stamp"
# Another version: the stamp's digit plus 1 (mod 10), whatever it is.
nam_edit nam_version 's/^=0 /=1 /;t;s/^=1 /=2 /;t;s/^=2 /=3 /;t;s/^=3 /=4 /;t
s/^=4 /=5 /;t;s/^=5 /=6 /;t;s/^=6 /=7 /;t;s/^=7 /=8 /;t;s/^=8 /=9 /;t;s/^=9 /=0 /'
nam_damaged_test nam_version "of another version"
nam_edit nam_long "s/^__putc\$/$(printf 'n%031d' 0)/"
nam_damaged_test nam_long "with a name over 31 characters"
./ccpp -o "$OUT/nam_stale" tests/run/hello.c
cp tests/run/hello.c "$OUT/nam_other.c"
./ccpp -o "$OUT/nam_other" "$OUT/nam_other.c"
mv "$OUT/nam_other.nam" "$OUT/nam_stale.nam"
rm -f "$OUT/nam_other.i"
nam_damaged_test nam_stale "from another run of ccpp"

# cccomp finds a damaged token stream (message 230 or 231).
tok_damaged_test() {
    if ./cccomp "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL cccomp: accepted a token stream $2"; bad=1
    elif ! grep -qF "$3: $OUT/$1.i" "$OUT/$1.err"; then
        echo "FAIL cccomp: wrong message for a token stream $2:"
        sed 's/^/    /' "$OUT/$1.err"; bad=1
    elif [ -e "$OUT/$1.ir" ]; then
        echo "FAIL cccomp: a token stream $2 left an IR file"; bad=1
    fi
    rm -f "$OUT/$1.i" "$OUT/$1.nam"
}
./ccpp -o "$OUT/tok_magic" tests/run/hello.c
printf 'X' | dd of="$OUT/tok_magic.i" bs=1 seek=0 conv=notrunc 2>/dev/null
tok_damaged_test tok_magic "with a wrong magic" \
    "not a token file from ccpp, or the wrong version"
./ccpp -o "$OUT/tok_version" tests/run/hello.c
v=$(od -An -tu1 -j 3 -N1 "$OUT/tok_version.i" | tr -d ' ')
printf "\\$(printf '%03o' $(( (v + 1) % 256 )))" \
    | dd of="$OUT/tok_version.i" bs=1 seek=3 conv=notrunc 2>/dev/null
tok_damaged_test tok_version "of another version" \
    "not a token file from ccpp, or the wrong version"
./ccpp -o "$OUT/tok_cut" tests/run/hello.c
size=$(wc -c < "$OUT/tok_cut.i")
head -c $((size - 3)) "$OUT/tok_cut.i" > "$OUT/tok_cut.tmp"
mv "$OUT/tok_cut.tmp" "$OUT/tok_cut.i"
tok_damaged_test tok_cut "cut short" "token file is damaged"
./ccpp -o "$OUT/tok_code" tests/run/hello.c
printf '\377' | dd of="$OUT/tok_code.i" bs=1 seek=4 conv=notrunc 2>/dev/null
tok_damaged_test tok_code "with an unknown code" "token file is damaged"
./ccpp -o "$OUT/tok_after" tests/run/hello.c
printf ';' >> "$OUT/tok_after.i"
tok_damaged_test tok_after "with something after its stamp" \
    "token file is damaged"

# The per-function limits: main repeats statement $2, $3 times; must
# fail with message $4.
limit_test() {
    big=$OUT/$1.c
    {
        echo "int main() { int x;"
        i=0
        while [ $i -lt "$3" ]; do echo "$2"; i=$((i + 1)); done
        echo "return 0; }"
    } > "$big"
    if "$CCISH" -o "$OUT/$1" "$big" 2>"$OUT/$1.err"; then
        echo "FAIL $big: compiled, but should have failed"; bad=1
    elif grep -qF -- "$4" "$OUT/$1.err"; then
        :
    else
        echo "FAIL $big: wrong message. Wanted: $4"
        sed 's/^/    got: /' "$OUT/$1.err"; bad=1
    fi
}
limit_test too_large "x = 1000;" 900 "function too large: main"
limit_test too_many_relocs "__puti(1);" 300 \
    "too many calls and data references in main"

# The passes as programs of their own (approach.md, "B3", and 18.7):
# ccpp leaves the token stream and name file for cccomp, which removes
# the token stream and leaves the IR for ccgen, which writes the stream
# file and symbol summary and removes the IR and name files; ccrel then
# keeps those, as a linker keeps its objects.
if ./ccpp -o "$OUT/split_streams" tests/run/hello.c \
   && [ -s "$OUT/split_streams.i" ] && [ -s "$OUT/split_streams.nam" ] \
   && [ ! -e "$OUT/split_streams.ir" ] \
   && ./cccomp "$OUT/split_streams" && [ ! -e "$OUT/split_streams.i" ] \
   && [ -s "$OUT/split_streams.nam" ] \
   && [ -s "$OUT/split_streams.ir" ] && [ ! -e "$OUT/split_streams.obj" ] \
   && ./ccgen "$OUT/split_streams" && [ ! -e "$OUT/split_streams.ir" ] \
   && [ ! -e "$OUT/split_streams.nam" ] \
   && [ -s "$OUT/split_streams.obj" ] && [ -s "$OUT/split_streams.syms" ] \
   && ./ccrel "$OUT/split_streams" && [ -s "$OUT/split_streams" ] \
   && [ -s "$OUT/split_streams.obj" ] \
   && [ -s "$OUT/split_streams.syms" ]; then
    :
else
    echo "FAIL ccpp/cccomp/ccgen/ccrel: files not passed on, or not kept"
    bad=1
fi
rm -f "$OUT/split_streams.obj" "$OUT/split_streams.syms"
# ccgen -k keeps the IR file and name file, and cccomp -k its token
# stream.
if ./ccpp -o "$OUT/keep_ir" tests/run/hello.c \
   && ./cccomp -k "$OUT/keep_ir" && [ -s "$OUT/keep_ir.i" ] \
   && ./ccgen -k "$OUT/keep_ir" && [ -s "$OUT/keep_ir.ir" ] \
   && [ -s "$OUT/keep_ir.nam" ] \
   && [ -s "$OUT/keep_ir.obj" ]; then
    :
else
    echo "FAIL ccgen -k, cccomp -k: the IR file, token stream and name" \
         "file should be kept"
    bad=1
fi
rm -f "$OUT/keep_ir.ir" "$OUT/keep_ir.obj" "$OUT/keep_ir.syms" \
      "$OUT/keep_ir.i" "$OUT/keep_ir.nam"
# A compile error in cccomp leaves no IR file for ccgen, and keeps its
# inputs, as ccgen keeps its IR file.
if cc_ir "$OUT/split_err" tests/errors/const_assign.c 2>/dev/null \
   || [ -e "$OUT/split_err.ir" ] || [ ! -e "$OUT/split_err.i" ] \
   || [ ! -e "$OUT/split_err.nam" ]; then
    echo "FAIL cccomp: a compile error must fail, leave no IR file, and" \
         "keep its inputs"
    bad=1
fi
rm -f "$OUT/split_err.i" "$OUT/split_err.nam"
# A ccpp error leaves no token stream or name file, unless -k.
printf 'int main() { return 0; }\n"\n' >"$OUT/pp_err.c"
if ./ccpp -o "$OUT/pp_err" "$OUT/pp_err.c" 2>/dev/null \
   || [ -e "$OUT/pp_err.i" ] || [ -e "$OUT/pp_err.nam" ] \
   || ./ccpp -k -o "$OUT/pp_err" "$OUT/pp_err.c" 2>/dev/null \
   || [ ! -e "$OUT/pp_err.i" ]; then
    echo "FAIL ccpp: an error must fail and leave no output, unless -k"
    bad=1
fi
rm -f "$OUT/pp_err.i" "$OUT/pp_err.nam"
# A file name over MAX_PATH (128, with its NUL) is "output name too
# long" (diag.c's buffers are sized for ProDOS paths): 133 characters
# for the IR file's name.
long=$OUT/$(printf '%0120d' 0)
./ccpp -o "$OUT/long" tests/run/hello.c
if ./cccomp -o "$long" "$OUT/long" 2>"$OUT/long.err"; then
    echo "FAIL cccomp: accepted an output name over MAX_PATH"; bad=1
elif grep -qF "output name too long" "$OUT/long.err"; then
    :
else
    echo "FAIL cccomp: wanted 'output name too long', got:"
    sed 's/^/    /' "$OUT/long.err"; bad=1
fi
rm -f "$OUT/long.i" "$OUT/long.nam"
# The token streams, name files, IR files and stream files (.i, .nam,
# .ir, .obj and .syms) must be gone after every compile, successful or
# not, unless -k keeps them. Checked in a private directory, so it holds
# whenever it runs.
mkdir -p "$OUT/left"
if "$CCISH" -o "$OUT/left/hello" tests/run/hello.c 2>/dev/null; then
    : 
else
    echo "FAIL leftover: hello.c wouldn't compile"; bad=1
fi
left=$(find "$OUT/left" -name '*.i' -o -name '*.nam' -o -name '*.ir' \
       -o -name '*.obj' -o -name '*.syms')
if [ -n "$left" ]; then
    echo "FAIL token, name, IR or stream files left behind:"
    echo "$left" | sed 's/^/    /'; bad=1
fi
if "$CCISH" -k -o "$OUT/left/keep" tests/run/hello.c \
   && [ -s "$OUT/left/keep" ] && [ -s "$OUT/left/keep.ir" ] \
   && [ -s "$OUT/left/keep.i" ] && [ -s "$OUT/left/keep.nam" ] \
   && [ -s "$OUT/left/keep.obj" ] && [ -s "$OUT/left/keep.syms" ]; then
    :
else
    echo "FAIL -k: module, token stream, name file, IR file and stream"
         "files should all exist"
    bad=1
fi
rm -rf "$OUT/left"
exit $bad

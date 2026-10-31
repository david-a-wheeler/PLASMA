#!/bin/sh
# Run the ccish tests. Run from the ccomp directory (or via "make test").
#
# tests/run/NAME.c       compiled, run under plvm; stdout must equal
#                        tests/run/NAME.expected exactly. The source may
#                        also check the generated code (the -l listing)
#                        with comment lines anywhere in the file:
#                          // listing-has: TEXT      some line contains TEXT
#                          // listing-lacks: TEXT    no line contains TEXT
#                          // listing-count: N TEXT  exactly N lines do
#                        and the compiler's messages (warnings):
#                          // stderr-has: TEXT       some line contains TEXT
#                        A test with no stderr-has lines must compile with
#                        no messages at all.
# tests/errors/NAME.c    must fail to compile; stderr must contain the
#                        text after "// expect: " on the file's first line.
# Afterward, no IR or stream files may be left over, and -k must keep
# them. The passes as programs of their own, ccpp, cccomp, ccgen and
# ccrel, must write the same module as ccish for every run test. The
# message file must agree with the sources (tests/check_msgs.sh). The
# small build (ccpp-small, cccomp-small, ccgen-small, ccrel-small) must
# write the same token streams, objects and modules as the full build,
# for every run test and link test.
# tests/pp/NAME.c        ccpp's output, NAME.i and NAME.nam, must equal
#                        tests/pp/NAME.tok (the bytes of NAME.i in hex,
#                        "#" starting a comment) and tests/pp/NAME.nam,
#                        both written by hand from CCPP.md, but for the
#                        checksum: "ssss" in NAME.nam and "ss ss" in
#                        NAME.tok stand for it, as computed by
#                        nam_sum below.

set -u
CCISH=./ccish
PLVM=$(cd ../src && pwd)/plvm
OUT=tests/out
pass=0
fail=0

rm -rf "$OUT"
mkdir -p "$OUT"

# Compile source $3 into the object of output name $2 (its .obj and
# .syms) with the preprocessor, front end and back end: ccpp, cccomp,
# then ccgen; $1 is "" for the full build or "-small" for the small one.
cc_obj() {
    ./ccpp$1 -o "$2" "$3" && ./cccomp$1 "$2" && ./ccgen$1 "$2"
}

# Compile source $2 into the IR file of output name $1 ($1.ir) with
# ccpp and cccomp.
cc_ir() {
    ./ccpp -o "$1" "$2" && ./cccomp "$1"
}

# Check the listing directives in source $1 against listing $2.
# Prints each problem; returns 1 if there were any.
check_listing() {
    bad=0
    while IFS= read -r line; do
        case "$line" in
        "// listing-has: "*)
            text=${line#"// listing-has: "}
            if ! grep -qF -- "$text" "$2"; then
                echo "    listing has no line with: $text"
                bad=1
            fi ;;
        "// listing-lacks: "*)
            text=${line#"// listing-lacks: "}
            if grep -qF -- "$text" "$2"; then
                echo "    listing should not contain: $text"
                bad=1
            fi ;;
        "// listing-count: "*)
            rest=${line#"// listing-count: "}
            n=${rest%% *}
            text=${rest#* }
            c=$(grep -cF -- "$text" "$2")
            if [ "$c" -ne "$n" ]; then
                echo "    listing has $c lines with '$text', expected $n"
                bad=1
            fi ;;
        esac
    done < "$1"
    return $bad
}

# Check the compiler's messages ($2) against the stderr-has directives in
# source $1, or, if there are none, check that there were no messages.
# Prints each problem; returns 1 if there were any.
check_stderr() {
    if ! grep -q "^// stderr-has: " "$1"; then
        if [ -s "$2" ]; then
            echo "    unexpected compiler messages:"
            sed 's/^/      /' "$2"
            return 1
        fi
        return 0
    fi
    bad=0
    while IFS= read -r line; do
        case "$line" in
        "// stderr-has: "*)
            text=${line#"// stderr-has: "}
            if ! grep -qF -- "$text" "$2"; then
                echo "    compiler messages have no line with: $text"
                bad=1
            fi ;;
        esac
    done < "$1"
    return $bad
}

for src in tests/run/*.c; do
    name=$(basename "$src" .c)
    # plvm looks modules up by lowercase name with no extension.
    mod=$(echo "$name" | tr 'A-Z' 'a-z')
    if ! "$CCISH" -l -o "$OUT/$mod" "$src" >"$OUT/$name.lst" 2>"$OUT/$name.err"; then
        echo "FAIL $src: compile error"
        sed 's/^/    /' "$OUT/$name.err"
        fail=$((fail + 1))
        continue
    fi
    # plvm writes each newline as CR LF; compare with plain LF.
    (cd "$OUT" && timeout 10 "$PLVM" "$mod" 2>&1 | tr -d '\r' >"$name.actual")
    if ! cmp -s "$OUT/$name.actual" "tests/run/$name.expected"; then
        echo "FAIL $src: output differs (expected, then actual):"
        sed 's/^/    /' "tests/run/$name.expected"
        echo "    ---"
        sed 's/^/    /' "$OUT/$name.actual"
        fail=$((fail + 1))
    elif ! problems=$(check_listing "$src" "$OUT/$name.lst"); then
        echo "FAIL $src: generated code (see $OUT/$name.lst):"
        echo "$problems"
        fail=$((fail + 1))
    elif ! problems=$(check_stderr "$src" "$OUT/$name.err"); then
        echo "FAIL $src: compiler messages (see $OUT/$name.err):"
        echo "$problems"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

for src in tests/errors/*.c; do
    name=$(basename "$src" .c)
    want=$(head -n 1 "$src" | sed -n 's|^// expect: ||p')
    if [ -z "$want" ]; then
        echo "FAIL $src: first line has no '// expect: ' text"
        fail=$((fail + 1))
        continue
    fi
    if "$CCISH" -o "$OUT/err_$name" "$src" 2>"$OUT/$name.err"; then
        echo "FAIL $src: compiled, but should have failed"
        fail=$((fail + 1))
    elif grep -qF -- "$want" "$OUT/$name.err"; then
        pass=$((pass + 1))
    else
        echo "FAIL $src: wrong message. Wanted: $want"
        sed 's/^/    got: /' "$OUT/$name.err"
        fail=$((fail + 1))
    fi
done

# The checksum of name file $1 (CCPP.md, "The checksum"): over the
# bytes of its lines before the blank line, newlines included, as 4
# upper-case hex digits. Written apart from ccpp's code, so that an
# expected checksum never comes from the program under test.
nam_sum() {
    LC_ALL=C awk '
        BEGIN { for (i = 1; i < 256; i++) ord[sprintf("%c", i)] = i }
        $0 == "" { exit }
        {
            for (i = 1; i <= length($0); i++)
                h = (h * 31 + ord[substr($0, i, 1)]) % 65536
            h = (h * 31 + 10) % 65536
        }
        END { printf "%04X\n", h }' "$1"
}

# ccpp's output for each tests/pp/NAME.c must be tests/pp/NAME.tok and
# tests/pp/NAME.nam (written by hand from CCPP.md), with the checksum of
# the expected name file put in for "ssss" and "ss ss".
for src in tests/pp/*.c; do
    name=$(basename "$src" .c)
    sum=$(nam_sum "tests/pp/$name.nam")
    lo=$(echo "$sum" | cut -c3-4 | tr 'A-F' 'a-f')
    hi=$(echo "$sum" | cut -c1-2 | tr 'A-F' 'a-f')
    sed "s/ssss/$sum/" "tests/pp/$name.nam" >"$OUT/pp_$name.nam.want"
    sed -e 's/#.*//' -e "s/ss ss/$lo $hi/" "tests/pp/$name.tok" \
        | tr -s ' \t' '\n\n' | grep . >"$OUT/pp_$name.tok.want"
    if ! ./ccpp -o "$OUT/pp_$name" "$src" 2>"$OUT/pp_$name.err"; then
        echo "FAIL $src: ccpp failed"
        sed 's/^/    /' "$OUT/pp_$name.err"
        fail=$((fail + 1))
        continue
    fi
    od -An -v -tx1 "$OUT/pp_$name.i" | tr -s ' \t' '\n\n' | grep . \
        >"$OUT/pp_$name.tok.got"
    if ! cmp -s "$OUT/pp_$name.tok.want" "$OUT/pp_$name.tok.got"; then
        echo "FAIL $src: token stream differs (wanted, then got):"
        tr '\n' ' ' <"$OUT/pp_$name.tok.want" | fold -w 72 | sed 's/^/    /'
        echo
        echo "    ---"
        tr '\n' ' ' <"$OUT/pp_$name.tok.got" | fold -w 72 | sed 's/^/    /'
        echo
        fail=$((fail + 1))
    elif ! cmp -s "$OUT/pp_$name.nam.want" "$OUT/pp_$name.nam"; then
        echo "FAIL $src: name file differs:"
        diff "$OUT/pp_$name.nam.want" "$OUT/pp_$name.nam" | sed 's/^/    /'
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
    rm -f "$OUT/pp_$name.i" "$OUT/pp_$name.nam"
done

# ccpp's options (approach.md, "ccpp design", step 4): -D name=text,
# -D name (1), -U, in order; -I dir (or -Idir) for <file>, which is
# looked for only there; and the predefined __CCISH__. ccish takes them
# too. The program prints 7 2 1 0 1.
mkdir -p "$OUT/ppopt/inc"
printf '#define FROM_INC 1\n' >"$OUT/ppopt/inc/sys.h"
cat >"$OUT/ppopt/t.c" <<'EOF'
#include <sys.h>
#ifdef GONE
int gone = 1;
#endif
int main()
{
  puti(SEVEN); putc(' '); puti(TWO); putc(' '); puti(ONE); putc(' ');
#ifdef GONE
  puti(9);
#else
  puti(0);
#endif
  putc(' '); puti(__CCISH__ * FROM_INC); putln();
  return 0;
}
EOF
if ./ccish -o "$OUT/ppopt/t" -D SEVEN=7 -DTWO=2 -DONE -D GONE -U GONE \
        -I "$OUT/ppopt/inc" "$OUT/ppopt/t.c" 2>"$OUT/ppopt/t.err" \
   && [ "$(cd "$OUT/ppopt" && timeout 10 "$PLVM" t 2>&1 | tr -d '\r')" \
        = "7 2 1 0 1" ] \
   && ./ccpp -o "$OUT/ppopt/u" -I"$OUT/ppopt/inc" -DSEVEN=7 -DTWO -DONE \
        "$OUT/ppopt/t.c" 2>>"$OUT/ppopt/t.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccpp/ccish: -D, -U and -I don't work as they should"
    sed 's/^/    /' "$OUT/ppopt/t.err"
    fail=$((fail + 1))
fi
rm -f "$OUT/ppopt/u.i" "$OUT/ppopt/u.nam"
# <file> isn't looked for beside the including file, only in -I's.
cp "$OUT/ppopt/inc/sys.h" "$OUT/ppopt/sys.h"
if ./ccpp -o "$OUT/ppopt/v" "$OUT/ppopt/t.c" 2>"$OUT/ppopt/v.err" \
   || ! grep -qF "error 236: cannot open the #include file" \
        "$OUT/ppopt/v.err"; then
    echo "FAIL ccpp: found <sys.h> beside the including file, with no -I"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
# The macro table full (MAX_MACROS, 128): __CCISH__ is one, so the
# 128th #define is one too many.
{
    i=1
    while [ $i -le 129 ]; do echo "#define M$i $i"; i=$((i + 1)); done
} >"$OUT/ppopt/many.c"
if ./ccpp -o "$OUT/ppopt/many" "$OUT/ppopt/many.c" 2>"$OUT/ppopt/many.err" \
   || ! grep -qF "many.c:128: fatal 244: too many macros" \
        "$OUT/ppopt/many.err"; then
    echo "FAIL ccpp: 129 macros, wanted 'too many macros' on line 128, got:"
    sed 's/^/    /' "$OUT/ppopt/many.err"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
# Too many -I, -D and -U options (MAX_PP_OPTS, 16): 17 -D.
set --
i=1
while [ $i -le 17 ]; do set -- "$@" -D "X$i"; i=$((i + 1)); done
if ./ccpp "$@" -o "$OUT/ppopt/opts" "$OUT/ppopt/t.c" \
        2>"$OUT/ppopt/opts.err" \
   || ! grep -qF "fatal 248: too many -I, -D and -U options" \
        "$OUT/ppopt/opts.err"; then
    echo "FAIL ccpp: 17 -D options, wanted 'too many', got:"
    sed 's/^/    /' "$OUT/ppopt/opts.err"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
set --

# ccpp -E (full build only: CCISH_TEXT) writes the tokens as text, a
# space between them, each on its source line, with a line marker for a
# change of file; and no token stream or name file. (Written by hand.)
mkdir -p "$OUT/ppe"
printf '#define TWO 2\nint b;\n' >"$OUT/ppe/e.h"
printf '%s\n' '#define N 4' 'int a = N;' '#include "e.h"' '' \
    'int c = "x" "y" + TWO;' >"$OUT/ppe/e.c"
{
    printf '\nint a = N ;\n' | sed 's/N/4/'
    printf '# 2 "%s"\n' "$OUT/ppe/e.h"
    printf 'int b ;\n'
    printf '# 5 "%s"\n' "$OUT/ppe/e.c"
    printf 'int c = "xy" + 2 ;\n'
} >"$OUT/ppe/e.want"
if ./ccpp -E -o "$OUT/ppe/e" "$OUT/ppe/e.c" >"$OUT/ppe/e.got" \
        2>"$OUT/ppe/e.err" \
   && cmp -s "$OUT/ppe/e.want" "$OUT/ppe/e.got" \
   && [ ! -e "$OUT/ppe/e.i" ] && [ ! -e "$OUT/ppe/e.nam" ]; then
    pass=$((pass + 1))
else
    echo "FAIL ccpp -E: wanted, then got:"
    sed 's/^/    /' "$OUT/ppe/e.want"
    echo "    ---"
    sed 's/^/    /' "$OUT/ppe/e.got" "$OUT/ppe/e.err"
    fail=$((fail + 1))
fi
# The small build has no -E.
if ./ccpp-small -E -o "$OUT/ppe/s" "$OUT/ppe/e.c" >/dev/null 2>&1; then
    echo "FAIL ccpp-small: accepted -E"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi

# cccomp finds a damaged or stale name file (CCPP.md, "What cccomp
# checks"): each change below is made to hello.c's, which must then be
# refused with message 232, leaving no IR file.
nam_damaged_test() {
    if ./cccomp "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL cccomp: accepted a name file $2"
        fail=$((fail + 1))
    elif ! grep -qF "error 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err" && \
         ! grep -qF "fatal 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err"; then
        echo "FAIL cccomp: wrong message for a name file $2:"
        sed 's/^/    /' "$OUT/$1.err"
        fail=$((fail + 1))
    elif [ -e "$OUT/$1.ir" ]; then
        echo "FAIL cccomp: a name file $2 left an IR file"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
    rm -f "$OUT/$1.i" "$OUT/$1.nam"
}
# Make $OUT/$1.i and .nam from hello.c, then pass the name file through
# sed program $2.
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
nam_edit nam_count 's/^=1 \(....\)/=1 FFFF/'
nam_damaged_test nam_count "with a wrong count"
nam_edit nam_sum 's/^putc$/putd/'
nam_damaged_test nam_sum "with a wrong checksum"
nam_edit nam_after '$a\
x'
nam_damaged_test nam_after "with a line after the stamp"
nam_edit nam_version 's/^=1 /=2 /'
nam_damaged_test nam_version "of another version"
nam_edit nam_long "s/^putc\$/$(printf 'n%031d' 0)/"
nam_damaged_test nam_long "with a name over 31 characters"
# A name file from another run of ccpp: well formed, but its stamp is
# not the token stream's (here, it names another source file).
./ccpp -o "$OUT/nam_stale" tests/run/hello.c
cp tests/run/hello.c "$OUT/nam_other.c"
./ccpp -o "$OUT/nam_other" "$OUT/nam_other.c"
mv "$OUT/nam_other.nam" "$OUT/nam_stale.nam"
rm -f "$OUT/nam_other.i"
nam_damaged_test nam_stale "from another run of ccpp"

# cccomp finds a damaged token stream: not one (a wrong magic or
# version: message 230), or one cut short, with an unknown code, or with
# something after its stamp (231). It leaves no IR file.
tok_damaged_test() {
    if ./cccomp "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL cccomp: accepted a token stream $2"
        fail=$((fail + 1))
    elif ! grep -qF "$3: $OUT/$1.i" "$OUT/$1.err"; then
        echo "FAIL cccomp: wrong message for a token stream $2:"
        sed 's/^/    /' "$OUT/$1.err"
        fail=$((fail + 1))
    elif [ -e "$OUT/$1.ir" ]; then
        echo "FAIL cccomp: a token stream $2 left an IR file"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
    rm -f "$OUT/$1.i" "$OUT/$1.nam"
}
./ccpp -o "$OUT/tok_magic" tests/run/hello.c
printf 'X' | dd of="$OUT/tok_magic.i" bs=1 seek=0 conv=notrunc 2>/dev/null
tok_damaged_test tok_magic "with a wrong magic" \
    "not a token file from ccpp, or the wrong version"
./ccpp -o "$OUT/tok_version" tests/run/hello.c
printf '\002' | dd of="$OUT/tok_version.i" bs=1 seek=3 conv=notrunc \
    2>/dev/null
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

# Generated tests of the per-function limits: main repeats statement
# $2, $3 times; compiling must fail with message $4.
limit_test() {
    big=$OUT/$1.c
    {
        echo "int main() { int x;"
        i=0
        while [ $i -lt "$3" ]; do echo "$2"; i=$((i + 1)); done
        echo "return 0; }"
    } > "$big"
    if "$CCISH" -o "$OUT/$1" "$big" 2>"$OUT/$1.err"; then
        echo "FAIL $big: compiled, but should have failed"
        fail=$((fail + 1))
    elif grep -qF -- "$4" "$OUT/$1.err"; then
        pass=$((pass + 1))
    else
        echo "FAIL $big: wrong message. Wanted: $4"
        sed 's/^/    got: /' "$OUT/$1.err"
        fail=$((fail + 1))
    fi
}
# 900 x 5 bytes (CW, SLW) is over the 1280-byte function buffer.
limit_test too_large "x = 1000;" 900 "function too large: main"
# 300 calls is over the 128 relocations allowed in one function.
limit_test too_many_relocs "puti(1);" 300 \
    "too many calls and data references in main"

# The passes as programs of their own (approach.md, "B3", and 18.7):
# for every run test, ccpp, cccomp, ccgen and ccrel must write the same
# module as ccish.
split_bad=0
for src in tests/run/*.c; do
    name=$(basename "$src" .c)
    mod=$(echo "$name" | tr 'A-Z' 'a-z')
    if ! cc_obj "" "$OUT/split_$mod" "$src" 2>/dev/null \
       || ! ./ccrel "$OUT/split_$mod" 2>/dev/null \
       || ! cmp -s "$OUT/split_$mod" "$OUT/$mod"; then
        echo "FAIL $src: ccpp, cccomp, ccgen, ccrel don't write ccish's" \
             "module"
        split_bad=1
    fi
    rm -f "$OUT/split_$mod.obj" "$OUT/split_$mod.syms"
done
if [ $split_bad -eq 0 ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
fi
# The small build (approach.md decision 21) must write the same token
# streams, name files, objects and modules as the full build, for every
# run test.
small_bad=0
for src in tests/run/*.c; do
    name=$(basename "$src" .c)
    mod=$(echo "$name" | tr 'A-Z' 'a-z')
    if ! ./ccpp -o "$OUT/full_$mod" "$src" 2>/dev/null \
       || ! ./ccpp-small -o "$OUT/small_$mod" "$src" 2>/dev/null \
       || ! cmp -s "$OUT/full_$mod.i" "$OUT/small_$mod.i" \
       || ! cmp -s "$OUT/full_$mod.nam" "$OUT/small_$mod.nam"; then
        echo "FAIL $src: the small ccpp doesn't write the full build's" \
             "token stream and name file"
        small_bad=1
    fi
    rm -f "$OUT/full_$mod.i" "$OUT/full_$mod.nam" \
          "$OUT/small_$mod.i" "$OUT/small_$mod.nam"
    if ! cc_obj "" "$OUT/full_$mod" "$src" 2>/dev/null \
       || ! cc_obj -small "$OUT/small_$mod" "$src" 2>/dev/null \
       || ! cmp -s "$OUT/full_$mod.obj" "$OUT/small_$mod.obj" \
       || ! cmp -s "$OUT/full_$mod.syms" "$OUT/small_$mod.syms" \
       || ! ./ccrel-small "$OUT/small_$mod" 2>/dev/null \
       || ! cmp -s "$OUT/small_$mod" "$OUT/$mod"; then
        echo "FAIL $src: the small build doesn't write the full build's" \
             "object and module"
        small_bad=1
    fi
    rm -f "$OUT/full_$mod.obj" "$OUT/full_$mod.syms" \
          "$OUT/small_$mod.obj" "$OUT/small_$mod.syms"
done
if [ $small_bad -eq 0 ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
fi
# ccpp leaves the token stream and name file for cccomp, which removes
# the token stream and leaves the IR file for ccgen (approach.md 18.7),
# which writes the stream file and symbol summary and removes the IR
# file and the name file (it needs the names too); ccrel then keeps
# those, as a linker keeps its objects.
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
    pass=$((pass + 1))
else
    echo "FAIL ccpp/cccomp/ccgen/ccrel: files not passed on, or not kept"
    fail=$((fail + 1))
fi
rm -f "$OUT/split_streams.obj" "$OUT/split_streams.syms"
# ccgen -k keeps the IR file and name file, and cccomp -k its token
# stream.
if ./ccpp -o "$OUT/keep_ir" tests/run/hello.c \
   && ./cccomp -k "$OUT/keep_ir" && [ -s "$OUT/keep_ir.i" ] \
   && ./ccgen -k "$OUT/keep_ir" && [ -s "$OUT/keep_ir.ir" ] \
   && [ -s "$OUT/keep_ir.nam" ] \
   && [ -s "$OUT/keep_ir.obj" ]; then
    pass=$((pass + 1))
else
    echo "FAIL ccgen -k, cccomp -k: the IR file, token stream and name" \
         "file should be kept"
    fail=$((fail + 1))
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
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
rm -f "$OUT/split_err.i" "$OUT/split_err.nam"
# A ccpp error leaves no token stream or name file, unless -k.
printf 'int main() { return 0; }\n"\n' >"$OUT/pp_err.c"
if ./ccpp -o "$OUT/pp_err" "$OUT/pp_err.c" 2>/dev/null \
   || [ -e "$OUT/pp_err.i" ] || [ -e "$OUT/pp_err.nam" ] \
   || ./ccpp -k -o "$OUT/pp_err" "$OUT/pp_err.c" 2>/dev/null \
   || [ ! -e "$OUT/pp_err.i" ]; then
    echo "FAIL ccpp: an error must fail and leave no output, unless -k"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
rm -f "$OUT/pp_err.i" "$OUT/pp_err.nam"
# ccgen -i prints the IR as it reads it (IR.md; written by hand): the
# function's line record, then each op. main's body starts at line 2;
# puti returns nothing; falling off main's end returns 0, which the
# peephole stage drops later.
printf 'int main()\n{\n  puti(7);\n  return 0;\n}\n' >"$OUT/ir_dump.c"
printf '%s\n' '; line 2' 'IR FUNC     main' 'IR CONST    7' \
    'IR CALL     puti nargs=1' 'IR CONST    0' 'IR RETURN  ' \
    'IR CONST    0' 'IR RETURN  ' 'IR ENDFUNC  frame=0' >"$OUT/ir_dump.want"
if cc_ir "$OUT/ir_dump" "$OUT/ir_dump.c" \
   && ./ccgen -i "$OUT/ir_dump" >"$OUT/ir_dump.got" \
   && cmp -s "$OUT/ir_dump.want" "$OUT/ir_dump.got"; then
    pass=$((pass + 1))
else
    echo "FAIL ccgen -i: wanted, then got:"
    sed 's/^/    /' "$OUT/ir_dump.want"
    echo "    ---"
    sed 's/^/    /' "$OUT/ir_dump.got"
    fail=$((fail + 1))
fi
rm -f "$OUT/ir_dump.obj" "$OUT/ir_dump.syms"
# ccgen finds a damaged IR file (OBJECT.md, "What ccgen checks"): cut
# short, a wrong version, an unknown tag. It writes no stream files,
# and keeps the IR file (and the name file).
ir_damaged_test() {
    if ./ccgen "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccgen: accepted an IR file $2"
        fail=$((fail + 1))
    elif ! grep -qF "damaged: $OUT/$1.ir" "$OUT/$1.err"; then
        echo "FAIL ccgen: wrong message for an IR file $2:"
        sed 's/^/    /' "$OUT/$1.err"
        fail=$((fail + 1))
    elif [ -e "$OUT/$1.obj" ] || [ -e "$OUT/$1.syms" ] \
         || [ ! -e "$OUT/$1.ir" ]; then
        echo "FAIL ccgen: an IR file $2 left stream files, or was removed"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
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
# SWAP whose length is wrong, here the top block of *f() = 5 made 2
# ops long instead of 1 (CONST 5), is a damaged file, not wrong code.
# The SWAP is the first $B2 byte (IR_SWAP $32 with a byte operand)
# after the 8-byte header (whose stamp could hold one), and its length
# the byte after it. The small build doesn't check.
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
# "The stamp"): one damaged, and one from another run of ccpp, whose
# stamp isn't the IR file's. It writes no stream files, and keeps its
# inputs.
gen_nam_test() {
    if ./ccgen "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccgen: accepted a name file $2"
        fail=$((fail + 1))
    elif ! grep -qF "fatal 232: name file is stale or damaged: $OUT/$1.nam" \
            "$OUT/$1.err"; then
        echo "FAIL ccgen: wrong message for a name file $2:"
        sed 's/^/    /' "$OUT/$1.err"
        fail=$((fail + 1))
    elif [ -e "$OUT/$1.obj" ] || [ ! -e "$OUT/$1.ir" ] \
         || [ ! -e "$OUT/$1.nam" ]; then
        echo "FAIL ccgen: a name file $2 left stream files, or lost an input"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
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
if grep -qF "too_large.c:1: error 177: function too large: main" \
        "$OUT/too_large.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccgen: a back-end error should give the file and line:"
    sed 's/^/    /' "$OUT/too_large.err"
    fail=$((fail + 1))
fi
# ccrel names the file it can't open (not another in the same static
# buffer: the message is printed after the stream files are removed).
cc_obj "" "$OUT/noobj" tests/run/hello.c
rm -f "$OUT/noobj.obj"
if ./ccrel "$OUT/noobj" 2>"$OUT/noobj.err"; then
    echo "FAIL ccrel: succeeded without its stream file"
    fail=$((fail + 1))
elif grep -qF "cannot open $OUT/noobj.obj" "$OUT/noobj.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: wanted 'cannot open $OUT/noobj.obj', got:"
    sed 's/^/    /' "$OUT/noobj.err"
    fail=$((fail + 1))
fi
rm -f "$OUT/noobj.syms"
# ccrel finds a damaged stream file: here, one cut short, and one with an
# unknown record tag added at the end.
damaged_test() {
    if ./ccrel "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccrel: accepted a stream file $2"
        fail=$((fail + 1))
    elif grep -qE "damaged|unexpected end" "$OUT/$1.err"; then
        pass=$((pass + 1))
    else
        echo "FAIL ccrel: wrong message for a stream file $2:"
        sed 's/^/    /' "$OUT/$1.err"
        fail=$((fail + 1))
    fi
    rm -f "$OUT/$1.obj" "$OUT/$1.syms"
}
cc_obj "" "$OUT/cut" tests/run/global_init.c
size=$(wc -c < "$OUT/cut.obj")
head -c $((size - 3)) "$OUT/cut.obj" > "$OUT/cut.tmp"
mv "$OUT/cut.tmp" "$OUT/cut.obj"
damaged_test cut "cut short"
cc_obj "" "$OUT/badtag" tests/run/global_init.c
printf 'X' >> "$OUT/badtag.obj"
damaged_test badtag "with an unknown record tag"
# A summary whose function starts at the code's end, where no function
# can (it has at least one byte), is found damaged as it is read: the
# message names the summary, not the stream file. The only symbol is
# main; its offset is bytes 15-16, after the 13-byte header (C I
# version, then five words: data, strings and code sizes, symbol
# counts) and its flags and parameter count. It becomes the code size,
# bytes 7-8.
echo 'int main() { return 0; }' >"$OUT/atend.c"
cc_obj "" "$OUT/atend" "$OUT/atend.c"
dd if="$OUT/atend.syms" of="$OUT/atend.size" bs=1 skip=7 count=2 2>/dev/null
dd if="$OUT/atend.size" of="$OUT/atend.syms" bs=1 seek=15 conv=notrunc \
   2>/dev/null
rm -f "$OUT/atend.size"
if ./ccrel "$OUT/atend" 2>"$OUT/atend.err"; then
    echo "FAIL ccrel: accepted a summary with a function at the code's end"
    fail=$((fail + 1))
elif grep -qF "damaged: $OUT/atend.syms" "$OUT/atend.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: wanted the summary damaged, for a function at the end:"
    sed 's/^/    /' "$OUT/atend.err"
    fail=$((fail + 1))
fi
rm -f "$OUT/atend.obj" "$OUT/atend.syms"

# Linking several objects (approach.md issue 11, "Step 3"): each
# directory tests/link/NAME holds .c files, compiled by cccomp and ccgen
# and linked by ccrel in name order. With a file "expected", the module is run under
# plvm, and its output must equal it; the listing directives of the run
# tests may be in any of the .c files. With "expect-error", linking must
# fail with a message containing its text, and leave no module.
mkdir -p "$OUT/link"
for dir in tests/link/*/; do
    name=$(basename "$dir")
    mod=$(echo "$name" | tr 'A-Z' 'a-z')
    lerr=$OUT/link/$name.err
    objs=""
    compiled=1
    for src in "$dir"*.c; do
        obj=$OUT/link/${name}_$(basename "$src" .c)
        if ! cc_obj "" "$obj" "$src" 2>"$lerr"; then
            echo "FAIL $src: compile error"
            sed 's/^/    /' "$lerr"
            compiled=0
            break
        fi
        objs="$objs $obj"
    done
    if [ $compiled -eq 0 ]; then
        fail=$((fail + 1))
    elif [ -f "$dir/expect-error" ]; then
        want=$(cat "$dir/expect-error")
        if ./ccrel -o "$OUT/link/$mod" $objs 2>"$lerr"; then
            echo "FAIL $dir: linked, but should have failed"
            fail=$((fail + 1))
        elif ! grep -qF -- "$want" "$lerr"; then
            echo "FAIL $dir: wrong message. Wanted: $want"
            sed 's/^/    got: /' "$lerr"
            fail=$((fail + 1))
        elif [ -e "$OUT/link/$mod" ]; then
            echo "FAIL $dir: a failed link left its module"
            fail=$((fail + 1))
        else
            pass=$((pass + 1))
        fi
    elif ! ./ccrel -l -o "$OUT/link/$mod" $objs >"$OUT/link/$name.lst" \
            2>"$lerr"; then
        echo "FAIL $dir: link error"
        sed 's/^/    /' "$lerr"
        fail=$((fail + 1))
    else
        (cd "$OUT/link" && timeout 10 "$PLVM" "$mod" 2>&1 | tr -d '\r' \
            >"$name.actual")
        problems=""
        for src in "$dir"*.c; do
            problems=$problems$(check_listing "$src" "$OUT/link/$name.lst")
        done
        if ! cmp -s "$OUT/link/$name.actual" "$dir/expected"; then
            echo "FAIL $dir: output differs (expected, then actual):"
            sed 's/^/    /' "$dir/expected"
            echo "    ---"
            sed 's/^/    /' "$OUT/link/$name.actual"
            fail=$((fail + 1))
        elif [ -n "$problems" ]; then
            echo "FAIL $dir: generated code (see $OUT/link/$name.lst):"
            echo "$problems"
            fail=$((fail + 1))
        elif [ -s "$lerr" ]; then
            echo "FAIL $dir: unexpected messages:"
            sed 's/^/    /' "$lerr"
            fail=$((fail + 1))
        else
            pass=$((pass + 1))
        fi
    fi
    for obj in $objs; do
        rm -f "$obj.obj" "$obj.syms"
    done
done
# The small build links the same: for each link test that links, the
# small cccomp, ccgen and ccrel must write the full build's objects and
# module.
small_bad=0
for dir in tests/link/*/; do
    [ -f "$dir/expect-error" ] && continue
    name=$(basename "$dir")
    full="" small=""
    for src in "$dir"*.c; do
        b=$(basename "$src" .c)
        if ! cc_obj "" "$OUT/link/f_${name}_$b" "$src" 2>/dev/null \
           || ! cc_obj -small "$OUT/link/s_${name}_$b" "$src" \
                2>/dev/null \
           || ! cmp -s "$OUT/link/f_${name}_$b.obj" \
                "$OUT/link/s_${name}_$b.obj" \
           || ! cmp -s "$OUT/link/f_${name}_$b.syms" \
                "$OUT/link/s_${name}_$b.syms"; then
            echo "FAIL $src: the small build doesn't write the full" \
                 "build's object"
            small_bad=1
        fi
        full="$full $OUT/link/f_${name}_$b"
        small="$small $OUT/link/s_${name}_$b"
    done
    if ! ./ccrel -o "$OUT/link/f_$name" $full 2>/dev/null \
       || ! ./ccrel-small -o "$OUT/link/s_$name" $small 2>/dev/null \
       || ! cmp -s "$OUT/link/f_$name" "$OUT/link/s_$name"; then
        echo "FAIL $dir: the small build doesn't link the full build's" \
             "module"
        small_bad=1
    fi
    for obj in $full $small; do
        rm -f "$obj.obj" "$obj.syms"
    done
done
if [ $small_bad -eq 0 ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
fi
# The shared-name table full (approach.md issue 11, "Fitting the
# linker's tables"): 163 names fill all 163 slots and link, and b's use
# of a's g0 is found in the full table; one more name stops the link.
# The objects are generated, mostly unused declarations, which are
# shared names too: full_a defines g0 and declares g1..g81, full_b
# declares g82..g161 and defines use (which calls g0), and full_c
# declares g82..g163, one too many. (704 name bytes, within 1427.)
gen_decls() {
    i=$2
    while [ "$i" -le "$3" ]; do
        echo "void g$i(void);"
        i=$((i + 1))
    done >"$1"
}
gen_decls "$OUT/link/full_a.c" 1 81
echo 'void g0(void) {}' >>"$OUT/link/full_a.c"
gen_decls "$OUT/link/full_b.c" 82 161
echo 'void g0(void); void use(void) { g0(); }' >>"$OUT/link/full_b.c"
gen_decls "$OUT/link/full_c.c" 82 163
if cc_obj "" "$OUT/link/full_a" "$OUT/link/full_a.c" \
   && cc_obj "" "$OUT/link/full_b" "$OUT/link/full_b.c" \
   && ./ccrel -l -o "$OUT/link/full" "$OUT/link/full_a" "$OUT/link/full_b" \
      >"$OUT/link/full.lst" 2>"$OUT/link/full.err" \
   && grep -qF "; shared names: 163 of 163 slots" "$OUT/link/full.lst"; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: 163 shared names don't fill the table and link"
    sed 's/^/    /' "$OUT/link/full.err"
    fail=$((fail + 1))
fi
if cc_obj "" "$OUT/link/full_c" "$OUT/link/full_c.c" \
   && ! ./ccrel -o "$OUT/link/full_over" "$OUT/link/full_a" \
        "$OUT/link/full_c" 2>"$OUT/link/full_over.err" \
   && grep -qF "too many names shared between objects" \
        "$OUT/link/full_over.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: 164 shared names, wanted 'too many names', got:"
    sed 's/^/    /' "$OUT/link/full_over.err"
    fail=$((fail + 1))
fi
rm -f "$OUT"/link/full_[abc].obj "$OUT"/link/full_[abc].syms
# More functions in a module than one object may define (MAX_FUNCS,
# 302): many_a defines f1..f160, many_b f161..f320, all static so they
# don't fill the shared-name table, and each object shares only two
# wrappers, which call its first and last; main calls all four. Each
# fN returns N, so a wrong RLD function entry calls the wrong function.
gen_funcs() {
    i=$2
    while [ "$i" -le "$3" ]; do
        echo "static int f$i(void) { return $i; }"
        i=$((i + 1))
    done >"$1"
    echo "int $4_first(void) { return f$2(); }" >>"$1"
    echo "int $4_last(void) { return f$3(); }" >>"$1"
}
gen_funcs "$OUT/link/many_a.c" 1 160 a
gen_funcs "$OUT/link/many_b.c" 161 320 b
cat >>"$OUT/link/many_b.c" <<'EOF'
int a_first(void); int a_last(void);
int main()
{
  puti(a_first()); putc(' '); puti(a_last()); putc(' ');
  puti(b_first()); putc(' '); puti(b_last()); putc(10);
  return 0;
}
EOF
if cc_obj "" "$OUT/link/many_a" "$OUT/link/many_a.c" \
   && cc_obj "" "$OUT/link/many_b" "$OUT/link/many_b.c" \
   && ./ccrel -o "$OUT/link/many" "$OUT/link/many_a" "$OUT/link/many_b" \
      2>"$OUT/link/many.err" \
   && [ "$(cd "$OUT/link" && timeout 10 "$PLVM" many 2>&1 | tr -d '\r')" \
        = "1 160 161 320" ]; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: 324 functions in two objects don't link and run"
    sed 's/^/    /' "$OUT/link/many.err"
    fail=$((fail + 1))
fi
rm -f "$OUT"/link/many_[ab].obj "$OUT"/link/many_[ab].syms
# More relocations than ccrel once kept in memory (2432; B4 rereads them
# instead): f1..f25 each store to the global g 100 times, one data
# relocation each, 2500 in all. main calls the first and the last, so a
# wrong RLD entry stores elsewhere or calls the wrong function.
{
    echo 'int g;'
    f=1
    while [ "$f" -le 25 ]; do
        echo "void f$f(void)"
        echo '{'
        i=1
        while [ "$i" -le 100 ]; do
            echo "  g = $((f * 1000 + i));"
            i=$((i + 1))
        done
        echo '}'
        f=$((f + 1))
    done
    echo 'int main()'
    echo '{'
    echo '  f1(); puti(g); putc(32); f25(); puti(g); putc(10);'
    echo '  return 0;'
    echo '}'
} >"$OUT/link/relocs.c"
if cc_obj "" "$OUT/link/relocs" "$OUT/link/relocs.c" \
      2>"$OUT/link/relocs.err" \
   && ./ccrel "$OUT/link/relocs" 2>>"$OUT/link/relocs.err" \
   && [ "$(cd "$OUT/link" && timeout 10 "$PLVM" relocs 2>&1 | tr -d '\r')" \
        = "1100 25100" ]; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: 2500 relocations don't link and run"
    sed 's/^/    /' "$OUT/link/relocs.err"
    fail=$((fail + 1))
fi
rm -f "$OUT"/link/relocs.obj "$OUT"/link/relocs.syms
# A static function's name that doesn't fit after the shared names is
# listed by its address (approach.md issue 11, "Item 1: the linker's
# local names"). 43 declared names and the caller, each 31 characters
# (32 bytes), and main, puti and putc (5 each) take 1423 of the 1427
# name bytes, leaving 4: too few for the static function's 31-character
# name, which is then listed, as defined and as called, by its address.
# The module still links and runs. The declared names are split
# between two objects, so that the link has two objects' names.
gen_long() {
    i=$2
    while [ "$i" -le "$3" ]; do
        printf 'void n%030d(void);\n' "$i"
        i=$((i + 1))
    done >"$1"
}
gen_long "$OUT/link/nofit_a.c" 1 22
gen_long "$OUT/link/nofit_b.c" 23 43
cat >>"$OUT/link/nofit_b.c" <<'EOF'
static int static_padded_to_thirty_one_chr(void) { return 7; }
int caller_padded_to_thirty_one_chr(void)
{
  return static_padded_to_thirty_one_chr();
}
int main()
{
  puti(caller_padded_to_thirty_one_chr()); putc(10);
  return 0;
}
EOF
if cc_obj "" "$OUT/link/nofit_a" "$OUT/link/nofit_a.c" \
      2>"$OUT/link/nofit.err" \
   && cc_obj "" "$OUT/link/nofit_b" "$OUT/link/nofit_b.c" \
      2>>"$OUT/link/nofit.err" \
   && ./ccrel -l -o "$OUT/link/nofit" "$OUT/link/nofit_a" \
      "$OUT/link/nofit_b" >"$OUT/link/nofit.lst" 2>>"$OUT/link/nofit.err" \
   && grep -qF "; shared names: 47 of 163 slots, 1423 of 1427" \
        "$OUT/link/nofit.lst" \
   && grep -q '^static@\$[0-9A-F]\{4\}:$' "$OUT/link/nofit.lst" \
   && grep -qF '; static@$' "$OUT/link/nofit.lst" \
   && ! grep -qF "static_padded" "$OUT/link/nofit.lst" \
   && [ "$(cd "$OUT/link" && timeout 10 "$PLVM" nofit 2>&1 | tr -d '\r')" \
        = "7" ]; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: a static name that doesn't fit isn't listed by address"
    sed 's/^/    /' "$OUT/link/nofit.err"
    fail=$((fail + 1))
fi
rm -f "$OUT"/link/nofit_[ab].obj "$OUT"/link/nofit_[ab].syms
# Two objects whose copies of a helper differ (compiled by different
# ccgens) are an error, not a silent merge: here one checksum changes.
# A summary ends with its helpers: the count, then for each its number,
# size, checksum, name length and name; for udiv alone, the checksum's
# low byte is the 7th byte from the end.
cc_obj "" "$OUT/link/hm_a" tests/link/helpers/a.c
cc_obj "" "$OUT/link/hm_b" tests/link/helpers/b.c
size=$(wc -c < "$OUT/link/hm_b.syms")
b=$(od -An -tu1 -j $((size - 7)) -N1 "$OUT/link/hm_b.syms" | tr -d ' ')
printf "\\$(printf '%03o' $(( (b + 1) % 256 )))" \
    | dd of="$OUT/link/hm_b.syms" bs=1 seek=$((size - 7)) conv=notrunc \
         2>/dev/null
if ./ccrel -o "$OUT/link/hm" "$OUT/link/hm_a" "$OUT/link/hm_b" \
       2>"$OUT/link/hm.err"; then
    echo "FAIL ccrel: linked objects whose helpers differ"
    fail=$((fail + 1))
elif grep -qF "helper functions differ between objects" "$OUT/link/hm.err"
then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: wanted 'helper functions differ', got:"
    sed 's/^/    /' "$OUT/link/hm.err"
    fail=$((fail + 1))
fi
rm -f "$OUT"/link/hm_[ab].obj "$OUT"/link/hm_[ab].syms
# Linking several objects needs a module name.
if ./ccrel "$OUT/a" "$OUT/b" 2>"$OUT/link/noname.err"; then
    echo "FAIL ccrel: linked several objects without -o"
    fail=$((fail + 1))
elif grep -qF "usage: ccrel" "$OUT/link/noname.err"; then
    pass=$((pass + 1))
else
    echo "FAIL ccrel: wanted its usage for several objects without -o, got:"
    sed 's/^/    /' "$OUT/link/noname.err"
    fail=$((fail + 1))
fi

# The message file must agree with the sources (approach.md, "C2").
if ./tests/check_msgs.sh; then
    pass=$((pass + 1))
else
    echo "FAIL tests/check_msgs.sh: ccish.msg and the sources disagree"
    fail=$((fail + 1))
fi
# Without the message file, a message is just its numbers (here 41,
# "lvalue required as", with part 194, "left operand of assignment").
mkdir -p "$OUT/nomsg"
cp "$CCISH" "$OUT/nomsg/ccish"
printf 'int main() { 1 = 2; return 0; }\n' > "$OUT/nomsg/t.c"
if "$OUT/nomsg/ccish" -o "$OUT/nomsg/t" "$OUT/nomsg/t.c" 2>"$OUT/nomsg/t.err"
then
    echo "FAIL no message file: compiled, but should have failed"
    fail=$((fail + 1))
elif grep -qF "t.c:1: error 41 (194):" "$OUT/nomsg/t.err"; then
    pass=$((pass + 1))
else
    echo "FAIL no message file: wanted 'error 41 (194):', got:"
    sed 's/^/    /' "$OUT/nomsg/t.err"
    fail=$((fail + 1))
fi

# A file name over MAX_PATH (128, with its NUL) is "output name too
# long" (diag.c's buffers are sized for ProDOS paths): here, 133
# characters for the IR file's name. (Once, removing the files after
# that error hit the same error again, endlessly.)
long=$OUT/$(printf '%0120d' 0)
./ccpp -o "$OUT/long" tests/run/hello.c
if ./cccomp -o "$long" "$OUT/long" 2>"$OUT/long.err"; then
    echo "FAIL cccomp: accepted an output name over MAX_PATH"
    fail=$((fail + 1))
elif grep -qF "output name too long" "$OUT/long.err"; then
    pass=$((pass + 1))
else
    echo "FAIL cccomp: wanted 'output name too long', got:"
    sed 's/^/    /' "$OUT/long.err"
    fail=$((fail + 1))
fi
rm -f "$OUT/long.i" "$OUT/long.nam"

# The token streams, name files, IR files and stream files (.i, .nam,
# .ir, .obj and .syms) must be gone after every compile, successful or
# not, unless -k keeps them.
left=$(find "$OUT" -name '*.i' -o -name '*.nam' -o -name '*.ir' \
       -o -name '*.obj' -o -name '*.syms')
if [ -n "$left" ]; then
    echo "FAIL token, name, IR or stream files left behind:"
    echo "$left" | sed 's/^/    /'
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
if "$CCISH" -k -o "$OUT/keep" tests/run/hello.c \
   && [ -s "$OUT/keep" ] && [ -s "$OUT/keep.ir" ] && [ -s "$OUT/keep.i" ] \
   && [ -s "$OUT/keep.nam" ] \
   && [ -s "$OUT/keep.obj" ] && [ -s "$OUT/keep.syms" ]; then
    pass=$((pass + 1))
else
    echo "FAIL -k: module, token stream, name file, IR file and stream" \
         "files should all exist"
    fail=$((fail + 1))
fi

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

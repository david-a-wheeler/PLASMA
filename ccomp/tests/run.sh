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
# Afterward, no stream files may be left over, and -k must keep them.
# The passes as programs of their own, cccomp then ccrel, must write
# the same module as ccish for every run test. The message file must
# agree with the sources (tests/check_msgs.sh).

set -u
CCISH=./ccish
PLVM=$(cd ../src && pwd)/plvm
OUT=tests/out
pass=0
fail=0

rm -rf "$OUT"
mkdir -p "$OUT"

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

# The passes as programs of their own (approach.md, "B3"): for every
# run test, cccomp then ccrel must write the same module as ccish.
split_bad=0
for src in tests/run/*.c; do
    name=$(basename "$src" .c)
    mod=$(echo "$name" | tr 'A-Z' 'a-z')
    if ! ./cccomp -o "$OUT/split_$mod" "$src" 2>/dev/null \
       || ! ./ccrel "$OUT/split_$mod" 2>/dev/null \
       || ! cmp -s "$OUT/split_$mod" "$OUT/$mod"; then
        echo "FAIL $src: cccomp then ccrel don't write ccish's module"
        split_bad=1
    fi
done
if [ $split_bad -eq 0 ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
fi
# cccomp leaves the stream files for ccrel, and ccrel removes them.
if ./cccomp -o "$OUT/split_streams" tests/run/hello.c \
   && [ -s "$OUT/split_streams.code" ] && [ -s "$OUT/split_streams.syms" ] \
   && ./ccrel "$OUT/split_streams" && [ -s "$OUT/split_streams" ] \
   && [ ! -e "$OUT/split_streams.code" ]; then
    pass=$((pass + 1))
else
    echo "FAIL cccomp/ccrel: stream files not passed on, or not removed"
    fail=$((fail + 1))
fi
# A compile error in cccomp leaves no stream files for ccrel.
if ./cccomp -o "$OUT/split_err" tests/errors/const_assign.c 2>/dev/null \
   || [ -e "$OUT/split_err.code" ] || [ -e "$OUT/split_err.syms" ]; then
    echo "FAIL cccomp: a compile error must fail and leave no stream files"
    fail=$((fail + 1))
else
    pass=$((pass + 1))
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

# The stream files (.data, .drel, .strs, .code, .syms) must be gone
# after every compile, successful or not, unless -k keeps them.
left=$(find "$OUT" -name '*.data' -o -name '*.drel' -o -name '*.strs' \
       -o -name '*.code' -o -name '*.syms')
if [ -n "$left" ]; then
    echo "FAIL stream files left behind:"
    echo "$left" | sed 's/^/    /'
    fail=$((fail + 1))
else
    pass=$((pass + 1))
fi
if "$CCISH" -k -o "$OUT/keep" tests/run/hello.c \
   && [ -s "$OUT/keep" ] && [ -e "$OUT/keep.data" ] \
   && [ -e "$OUT/keep.drel" ] && [ -s "$OUT/keep.strs" ] \
   && [ -s "$OUT/keep.code" ] && [ -s "$OUT/keep.syms" ]; then
    pass=$((pass + 1))
else
    echo "FAIL -k: module and stream files should all exist"
    fail=$((fail + 1))
fi

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

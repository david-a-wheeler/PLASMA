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
# 900 x 5 bytes (CW, SLW) is over the 4096-byte function buffer.
limit_test too_large "x = 1000;" 900 "function too large: main"
# 300 calls is over the 256 relocations allowed in one function.
limit_test too_many_relocs "puti(1);" 300 \
    "too many calls and data references in main"

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

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
# tests/errors/NAME.c    must fail to compile; stderr must contain the
#                        text after "// expect: " on the file's first line.

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

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

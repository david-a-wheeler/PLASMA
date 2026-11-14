#!/bin/sh
# The link test for tests/link/$1/: compile its .c files (name order), link
# them, and either run the module under plvm and compare its output to
# "expected" and its exit status to "status" (0 if there is none), or,
# with "expect-error", require the link to fail with a message equal to
# that file and leave no module behind.
. "$(dirname "$0")/lib.sh"
name=$1
dir=tests/link/$name/
mod=$(echo "$name" | tr 'A-Z' 'a-z')
lerr=$OUT/link/$name.err
mkdir -p "$OUT/link"
objs=""
for src in "$dir"*.c; do
    obj=$OUT/link/${name}_$(basename "$src" .c)
    if ! cc_obj "" "$obj" "$src" 2>"$lerr"; then
        echo "FAIL $src: compile error:"; sed 's/^/    /' "$lerr"; exit 1
    fi
    objs="$objs $obj"
done
if [ -f "$dir/expect-error" ]; then
    if ./cclink -o "$OUT/link/$mod" $objs 2>"$lerr"; then
        echo "FAIL $dir: linked, but should have failed"; exit 1
    elif ! grep -qF -f "$dir/expect-error" "$lerr"; then
        echo "FAIL $dir: wrong message (wanted, then got):"
        sed 's/^/    want: /' "$dir/expect-error"; sed 's/^/    got:  /' "$lerr"; exit 1
    elif [ -e "$OUT/link/$mod" ]; then
        echo "FAIL $dir: a failed link left its module"; exit 1
    fi
else
    if ! ./cclink -v -o "$OUT/link/$mod" $objs >"$OUT/link/$name.lst" 2>"$lerr"; then
        echo "FAIL $dir: link error:"; sed 's/^/    /' "$lerr"; exit 1
    fi
    run_module "$OUT/link" "$mod" >"$OUT/link/$name.actual"
    got=$?
    want=$(cat "$dir/status" 2>/dev/null || echo 0)
    if [ "$got" != "$want" ]; then
        echo "FAIL $dir: exit status $got, expected $want"; exit 1
    fi
    if ! diff -q "$OUT/link/$name.actual" "$dir/expected" >/dev/null; then
        echo "FAIL $dir: output differs (expected, then actual):"
        sed 's/^/    /' "$dir/expected"; echo "    ---"; sed 's/^/    /' "$OUT/link/$name.actual"; exit 1
    fi
    for src in "$dir"*.c; do
        tests/check_listing.sh "$src" "$OUT/link/$name.lst" || exit 1
    done
    if [ -s "$lerr" ]; then
        echo "FAIL $dir: unexpected messages:"; sed 's/^/    /' "$lerr"; exit 1
    fi
fi
for obj in $objs; do rm -f "$obj.obj" "$obj.syms"; done
exit 0

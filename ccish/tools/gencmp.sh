#!/bin/sh
# gencmp.sh: does this tree's ccish write the same code as BASE's?
#
# Usage: tools/gencmp.sh BASE
#   BASE  another ccish directory, built (make), such as a git worktree
#         of an earlier commit: git worktree add --detach
#         /tmp/claude-1000/wb HEAD~1, then make -C /tmp/claude-1000/wb/ccish
#
# For a change meant to make the compiler smaller or faster without
# changing what it writes. The same inputs, this tree's sources, go
# through both trees' passes (the full build): each test's source and
# ccish's own (its programs', its library's and XLOAD's) through ccpp,
# cccomp and ccgen, each pass reading its own pipeline's files, and
# the files each pass writes are compared byte for byte: NAME.i and
# NAME.nam (ccpp), NAME.ir (cccomp), NAME.obj and NAME.syms (ccgen).
# Then each tree's cclink links ccish's four programs from its own
# objects, with -L and its own library objects, as a REL module and
# as an XLOAD image (-x), and those are compared too. Each source whose
# output differs is named with the first pass that differs, as is one
# that one tree compiles and the other doesn't, and each program that
# differs. ccish's own sources are compiled as measure.py does
# (-DCCISH_SMALL), and the xrun tests with -I inc, as the Makefile
# does. Every compile has -I lib/include, this tree's headers, ahead of
# ccpp's default (its own tree's, by an absolute path that the name
# file records), so that both trees read the same headers by the same
# name. Exits 1 if anything differs. Deterministic: the sources in
# a fixed order, and no paths or times in the output.
set -u
[ $# -eq 1 ] || { echo "usage: $0 BASE" >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
base=$(cd "$1" && pwd) || exit 2
cd "$here"
for p in ccpp cccomp ccgen cclink; do
    for d in "$base" "$here"; do
        [ -x "$d/$p" ] || { echo "gencmp: no $d/$p (make)" >&2; exit 2; }
    done
done
out=tests/out/gencmp
rm -rf "$out"
for side in base this; do
    mkdir -p "$out/$side/src" "$out/$side/lib"
done

# Run source $3 through the passes in directory $1, as output name $2,
# with ccpp's options $4: 0 if all three passes succeed. -k keeps the
# stream files, which a pass removes once it has read them.
pipeline() {
    "$1/ccpp" -k $4 -o "$2" "$3" >/dev/null 2>&1 \
        && "$1/cccomp" -k -o "$2" "$2" >/dev/null 2>&1 \
        && "$1/ccgen" -k "$2" >/dev/null 2>&1
}

n=0 same=0 neither=0 bad=0
for src in tests/run/*.c tests/gen/*.c tests/xrun/*.c tests/link/*/*.c \
           src/*.c lib/*.c xload/*.c; do
    n=$((n + 1))
    opts="-I lib/include"
    name=f$n
    case $src in
    src/*) opts="$opts -DCCISH_SMALL" name=src/$(basename "$src" .c) ;;
    lib/*) name=lib/$(basename "$src" .c) ;;
    tests/xrun/*) opts="$opts -I inc" ;;
    esac
    pipeline "$base" "$out/base/$name" "$src" "$opts"; s1=$?
    pipeline "$here" "$out/this/$name" "$src" "$opts"; s2=$?
    if [ $s1 -ne 0 ] && [ $s2 -ne 0 ]; then
        neither=$((neither + 1))
        continue
    fi
    if [ $s1 -ne 0 ] || [ $s2 -ne 0 ]; then
        echo "differs: $src: compiled by only one (BASE $s1, this $s2)"
        bad=1
        continue
    fi
    first=
    for suffix in i nam ir obj syms; do
        if ! cmp -s "$out/base/$name.$suffix" "$out/this/$name.$suffix"
        then
            first=$suffix
            break
        fi
    done
    case $first in
    "") same=$((same + 1)) ;;
    i|nam) echo "differs: $src: ccpp's .$first"; bad=1 ;;
    ir) echo "differs: $src: cccomp's .ir"; bad=1 ;;
    *) echo "differs: $src: ccgen's .$first"; bad=1 ;;
    esac
done
echo "$n sources: $same the same, $neither compiled by neither"

# ccish's four programs, each linked by each tree's cclink from its own
# objects, in the Makefile's order, and its own library's.
for side in base this; do
    python3 tools/libmap.py "$out/$side/lib" \
        || { echo "gencmp: no lib.map for $side" >&2; exit 2; }
done
make -s print-sources | while read -r prog srcs; do
    [ "$prog" = lib ] && continue
    for side in base this; do
        [ $side = base ] && d=$base || d=$here
        objs=
        for s in $srcs; do
            objs="$objs $out/$side/src/$(basename "$s" .c)"
        done
        "$d/cclink" -L "$out/$side/lib" -o "$out/$side/$prog" $objs \
            >/dev/null 2>&1 \
        && "$d/cclink" -x -L "$out/$side/lib" -o "$out/$side/$prog.img" \
            $objs >/dev/null 2>&1 \
        || echo "differs: $prog: $side's cclink failed"
    done
    for f in "$prog" "$prog.img"; do
        cmp -s "$out/base/$f" "$out/this/$f" || echo "differs: $f"
    done
done >"$out/programs"
cat "$out/programs"
grep -q . "$out/programs" && bad=1
[ $bad -eq 0 ] && echo "gencmp: the same output"
exit $bad

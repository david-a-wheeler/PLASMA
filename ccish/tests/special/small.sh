#!/bin/sh
# The small build (approach.md decision 21) must write the same token
# streams, name files, objects and modules as the full build, for every
# run, cross, code-generation and link test, and the samples, the user
# programs. The samples carry the peaks the tests don't (rodfast.c's
# 232-byte macro expansion, its 1223-byte main): the limits' real
# floor. Self-contained: it compiles each source with both builds and
# compares the intermediates.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT/small"

# $1 the sources, space separated, $2 an extra ccpp option (""), $3 a
# tag for the file names, and $4 the library objects to link (a cross
# test's NAME.lib, or none). Each source is compiled with both builds:
# the small ccpp/cccomp/ccgen must write the full build's token stream,
# name file, object and symbol summary, and cclink-small the same
# module of the sources' objects (plus $4's).
same_build() {
    srcs=$1 extra=$2 tag=$3 libs=${4:-}
    full="" small=""
    for src in $srcs; do
        b=$(basename "$src" .c)
        if ! ./ccpp $extra -o "$OUT/small/$tag.$b.f" "$src" 2>/dev/null \
           || ! ./ccpp-small $extra -o "$OUT/small/$tag.$b.s" "$src" 2>/dev/null \
           || ! cmp -s "$OUT/small/$tag.$b.f.i" "$OUT/small/$tag.$b.s.i" \
           || ! cmp -s "$OUT/small/$tag.$b.f.nam" "$OUT/small/$tag.$b.s.nam"; then
            echo "FAIL $src: the small ccpp doesn't write the full" \
                 "build's token stream and name file"
            bad=1
        fi
        rm -f "$OUT/small/$tag.$b.f.i" "$OUT/small/$tag.$b.f.nam" \
              "$OUT/small/$tag.$b.s.i" "$OUT/small/$tag.$b.s.nam"
        if ! cc_obj "" "$OUT/small/$tag.$b.f" "$src" "$extra" 2>/dev/null \
           || ! cc_obj -small "$OUT/small/$tag.$b.s" "$src" "$extra" 2>/dev/null \
           || ! cmp -s "$OUT/small/$tag.$b.f.obj" "$OUT/small/$tag.$b.s.obj" \
           || ! cmp -s "$OUT/small/$tag.$b.f.syms" "$OUT/small/$tag.$b.s.syms"; then
            echo "FAIL $src: the small build doesn't write the full" \
                 "build's object and symbol summary"
            bad=1
        fi
        full="$full $OUT/small/$tag.$b.f"
        small="$small $OUT/small/$tag.$b.s"
    done
    if ! ./cclink -o "$OUT/small/$tag.f" $full $libs 2>/dev/null \
       || ! ./cclink-small -o "$OUT/small/$tag.s" $small $libs 2>/dev/null \
       || ! cmp -s "$OUT/small/$tag.f" "$OUT/small/$tag.s"; then
        echo "FAIL $srcs: the small build doesn't link the full" \
             "build's module"
        bad=1
    fi
    for obj in $full $small; do rm -f "$obj.obj" "$obj.syms"; done
    rm -f "$OUT/small/$tag.f" "$OUT/small/$tag.s"
}
i=0
for src in tests/run/*.c; do
    i=$((i + 1)); same_build "$src" "" "run$i"
done
i=0
for src in tests/xrun/*.c; do
    i=$((i + 1))
    libs=""
    if [ -f "${src%.c}.lib" ]; then
        for o in $(cat "${src%.c}.lib"); do libs="$libs $OUT/lib/$o"; done
    fi
    same_build "$src" "-I inc" "xrun$i" "$libs"
done
i=0
for src in tests/gen/*.c; do
    i=$((i + 1)); same_build "$src" "" "gen$i"
done
# The samples, the user programs: a limit shrink that still compiles
# ccish's own source and the tests can break them (rodfast.c's peaks).
# As samples/Makefile links them: rodfast with lores, the rest alone.
i=0
for src in samples/*.c; do
    i=$((i + 1))
    case $(basename "$src" .c) in
        rodfast) srcs="samples/rodfast.c samples/lores.c" ;;
        *) srcs="$src" ;;
    esac
    same_build "$srcs" "" "sample$i"
done
i=0
for dir in tests/link/*/; do
    [ -f "$dir/expect-error" ] && continue
    i=$((i + 1))
    full="" small=""
    for src in "$dir"*.c; do
        b=$(basename "$src" .c)
        if ! cc_obj "" "$OUT/small/l${i}_$b.f" "$src" 2>/dev/null \
           || ! cc_obj -small "$OUT/small/l${i}_$b.s" "$src" 2>/dev/null \
           || ! cmp -s "$OUT/small/l${i}_$b.f.obj" "$OUT/small/l${i}_$b.s.obj" \
           || ! cmp -s "$OUT/small/l${i}_$b.f.syms" "$OUT/small/l${i}_$b.s.syms"; then
            echo "FAIL $src: the small build doesn't write the full" \
                 "build's object"
            bad=1
        fi
        full="$full $OUT/small/l${i}_$b.f"
        small="$small $OUT/small/l${i}_$b.s"
    done
    if ! ./cclink -o "$OUT/small/lfull_$i" $full 2>/dev/null \
       || ! ./cclink-small -o "$OUT/small/lsmall_$i" $small 2>/dev/null \
       || ! cmp -s "$OUT/small/lfull_$i" "$OUT/small/lsmall_$i"; then
        echo "FAIL $dir: the small build doesn't link the full build's" \
             "module"
        bad=1
    fi
    for obj in $full $small; do rm -f "$obj.obj" "$obj.syms"; done
    rm -f "$OUT/small/lfull_$i" "$OUT/small/lsmall_$i"
done
exit $bad

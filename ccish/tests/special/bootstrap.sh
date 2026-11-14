#!/bin/sh
# The compiler bootstrap test (doc/bootstrap-test.md): ccish compiles
# itself, and then the result compiles itself again, and every object
# and module must come out byte-for-byte the same. Run by
# `make bootstrap` (or `make full-test`), not by the regular suite.
#
# Stage 1: the host's passes compile the four programs' sources (the
# small build) and the C library, one file per invocation (R1), and
# each program is linked with cclink's -L, the stage's lib and its
# lib.map; PROG.objs is the program's own objects, the same list at
# every stage. Stages 2 and 3: the same, under the VM through
# tools/ccrun, with the previous stage's modules as the passes (vccpp
# and the rest) and `cclink -L lib @PROG.objs` (R10) as the link. Each stage
# has its own directory, tests/out/boot/N, holding copies of the
# sources, sys/ccinc (lib/include, R2) and ccish.msg (diag looks for
# it beside the program). Stage 1's objects and modules must equal
# stage 2's (a host-only miscompile shows here: the ccgen of
# 15008d8c), and stage 2's stage 3's. On a difference, or a pass that
# fails, it says which and keeps the directories; else it removes them.
# It says what each stage is, then prints a dot per file compiled.
#
# The Makefile gives the sources: BOOT_ccpp, BOOT_cccomp, BOOT_ccgen and
# BOOT_cclink (each program's, as it builds them), and BOOT_LIB (the
# library's). BOOTSTRAP_VM is the VM.
. "$(dirname "$0")/../lib.sh"
PLVM=${BOOTSTRAP_VM:-$PLVM}
CCSTAT=$(pwd)/$OUT/ccstat
export PLVM CCSTAT
top=$(pwd)
BOOT=$top/$OUT/boot
PROGS="ccpp cccomp ccgen cclink"
TIMEOUT=60                      # seconds for any one pass

fail() {
    echo
    echo "bootstrap: $*"
    echo "bootstrap: kept $BOOT"
    exit 1
}

# Every source, once, in order: each program's, then the library's.
# An object is named for its source, so two sources can't share a name.
srcs=
for p in $PROGS; do
    eval "list=\${BOOT_$p:?the Makefile gives BOOT_$p}"
    for s in $list; do
        case " $srcs " in *" $s "*) ;; *) srcs="$srcs $s" ;; esac
    done
done
srcs="$srcs ${BOOT_LIB:?the Makefile gives BOOT_LIB}"
names=
for s in $srcs; do
    n=$(basename "$s" .c)
    case " $names " in *" $n "*) fail "two sources make $n" ;; esac
    names="$names $n"
done

# Make stage $1's directory: the sources, sys/ccinc and ccish.msg.
setup() {
    d=$BOOT/$1
    (cd "$top" && mkdir -p "$d/src" "$d/lib" "$d/sys" \
     && cp src/*.c src/*.h "$d/src/" && cp $BOOT_LIB "$d/lib/" \
     && cp -R lib/include "$d/sys/ccinc" && cp ccish.msg "$d/" \
     && cp "$top/$OUT/lib/lib.map" "$d/lib/") || exit 1
}

# Compile every source in stage $1's directory, with the passes $2 ccpp,
# $2 cccomp and $2 ccgen ($2 "host" for the host's, else "vm" for the
# modules vccpp and the rest, under the VM).
compile() {
    how=$2
    for s in $srcs; do
        n=$(basename "$s" .c)
        case $s in
        src/*) in=src/$n.c out=$n def=-DCCISH_SMALL ;;
        # The library's objects, in lib/, with lib.map: the stage's
        # lib is the library's directory, as LIB/ is on the CCLIB disk.
        *) in=lib/$n.c out=lib/$n def= ;;
        esac
        for pass in ccpp cccomp ccgen; do
            case $pass in
            ccpp) set -- $def -o "$out" "$in" ;;
            *) set -- "$out" ;;
            esac
            if [ "$how" = host ]; then
                "$top/$pass" "$@"
            else
                "$CCRUN" -t $TIMEOUT "v$pass" "$@"
            fi >>log 2>&1 || fail "stage $stage: $pass failed on $in (see $BOOT/$stage/log)"
        done
        printf .
    done
}

# Progress, so a long run doesn't look hung: begin stage $1 (on $2),
# and a dot for each file compiled; end it with its time.
nfiles=$(echo $names | wc -w)
begin() {
    t0=$(date +%s)
    printf "bootstrap: stage %d (%s) " $1 "$2"
}
end() {
    echo " $(($(date +%s) - t0))s"
}

# Stage 1, on the host.
rm -rf "$BOOT"
echo "bootstrap: 3 stages of $nfiles files each, a dot per file"
stage=1
setup 1
cd "$BOOT/1" || exit 1
echo "bootstrap: stage 1: the host's passes, built by cc, compile ccish's"
echo "bootstrap:   sources and its library, and link ccish's 4 programs"
begin 1 host
compile 1 host
for p in $PROGS; do
    eval "list=\$BOOT_$p"
    objs=
    for s in $list; do objs="$objs $(basename "$s" .c)"; done
    printf '%s\n' $objs >"$p.objs"
    "$top/cclink" -L lib -o "$p" "@$p.objs" \
        >>log 2>&1 || fail "stage 1: linking $p failed (see $BOOT/1/log)"
done
end

# Stages 2 and 3, under the VM, each with the stage before's modules.
for stage in 2 3; do
    prev=$((stage - 1))
    setup $stage
    cd "$BOOT/$stage" || exit 1
    for p in $PROGS; do
        cp "../$prev/$p" "v$p" && cp "../1/$p.objs" . || exit 1
    done
    if [ $stage = 2 ]; then
        echo "bootstrap: stage 2: stage 1's programs, PLASMA modules, run under"
        echo "bootstrap:   plvm and do the same"
    else
        echo "bootstrap: stage 3: stage 2's programs, compiled by ccish itself,"
        echo "bootstrap:   do it again; every stage's output must be identical"
    fi
    begin $stage plvm
    compile $stage vm
    for p in $PROGS; do
        "$CCRUN" -t $TIMEOUT vcclink -L lib -o "$p" "@$p.objs" >>log 2>&1 \
            || fail "stage $stage: linking $p failed (see $BOOT/$stage/log)"
    done
    end
done

# Stage 1 against 2, and 2 against 3: every object, in the order
# compiled, then every module.
echo "bootstrap: comparing the stages"
for a in 1 2; do
    b=$((a + 1))
    for s in $srcs; do
        n=$(basename "$s" .c)
        case $s in          # as compile's output names
        src/*) d= ;;
        *) d=lib/ ;;
        esac
        for f in "$d$n.obj" "$d$n.syms"; do
            cmp -s "$BOOT/$a/$f" "$BOOT/$b/$f" \
                || fail "stages $a and $b differ, first at $f"
        done
    done
    for p in $PROGS; do
        cmp -s "$BOOT/$a/$p" "$BOOT/$b/$p" \
            || fail "stages $a and $b differ at the module $p"
    done
done
cd "$top" && rm -rf "$BOOT"
echo "bootstrap: stages 1, 2 and 3 are the same"

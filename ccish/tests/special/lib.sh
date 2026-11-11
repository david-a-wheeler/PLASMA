#!/bin/sh
# The library checks gcc can't be the oracle for (the xrun tests cover
# string.h and stdio): main(argc, argv) through lib/start.c, with more
# words than it keeps; tools/plvmexit.c's exit from inside a call; and
# the link errors for main(argc, argv) without the start object and
# for exit without the stand-in.
. "$(dirname "$0")/../lib.sh"
D=$OUT/libspecial
L=$OUT/lib
bad=0
mkdir -p "$D"

# Compile C text $2 into object $D/$1.
src_obj() {
    printf '%s\n' "$2" > "$D/$1.c" && cc_obj "" "$D/$1" "$D/$1.c"
}

# Run module $1 in $D with arguments $2..., as run_module does (which
# takes none): its output, CRs removed, and ccrun's status.
run_args() {
    m=$1
    shift
    (cd "$D" && "$CCRUN" -t 10 "$m" "$@" >"$m.run" 2>&1)
    set -- $?
    tr -d '\r' <"$D/$m.run"
    rm -f "$D/$m.run"
    return "$1"
}

# Expect $1 to equal $2, for check $3.
same() {
    [ "$1" = "$2" ] || { echo "FAIL lib $3: wanted, then got:"; echo "    $2"; echo "    $1"; bad=1; }
}

src_obj args '#include <stdio.h>
int main(int argc, char **argv)
{
  int i;
  putc((char)(0x30 + argc / 10), stdout);
  putc((char)(0x30 + argc % 10), stdout);
  for (i = 0; i < argc; i++) {
    putc(32, stdout);
    fputs(argv[i], stdout);
  }
  fputs(argv[argc] ? " no NULL" : " NULL", stdout);
  putc(10, stdout);
  return 0;
}' || bad=1
./ccrel -o "$D/args" "$D/args" "$L/start" "$L/putc" "$L/fputs" "$L/strlen" || bad=1
out=$(run_args args x yy zzz) || { echo "FAIL lib args: status $?"; bad=1; }
same "$out" "04 args x yy zzz NULL" "args"
out=$(run_args args) || bad=1
same "$out" "01 args NULL" "no args"
# 30 words: start keeps the first 24 (MAX_ARGS), the module's name too.
out=$(run_args args 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29) || bad=1
same "$out" "24 args 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 NULL" "many args"

# exit from inside a call stops with its status, after what was printed
# (plvm's BRK message, on stderr, may come first).
src_obj quit '#include <stdlib.h>
void deep(int n) { if (n) deep(n - 1); else exit(3); }
int main(void) { __puts(pstr("before")); __putln(); deep(5); return 9; }' || bad=1
./ccrel -o "$D/quit" "$D/quit" "$L/plvmexit" || bad=1
out=$(run_args quit)
st=$?
same "$st" 3 "exit status"
case "$out" in
    *before*) ;;
    *) echo "FAIL lib exit: output '$out'"; bad=1 ;;
esac

# The link errors: no start object, and no exit.
if ./ccrel -o "$D/nostart" "$D/args" "$L/putc" "$L/fputs" "$L/strlen" 2>"$D/nostart.err"; then
    echo "FAIL lib: main(argc, argv) linked without lib/start"; bad=1
fi
grep -qF "undefined reference to '__start'" "$D/nostart.err" \
    || { echo "FAIL lib: wrong message without lib/start:"; cat "$D/nostart.err"; bad=1; }
if ./ccrel -o "$D/noexit" "$D/quit" 2>"$D/noexit.err"; then
    echo "FAIL lib: exit linked without tools/plvmexit"; bad=1
fi
grep -qF "undefined reference to 'exit'" "$D/noexit.err" \
    || { echo "FAIL lib: wrong message without an exit:"; cat "$D/noexit.err"; bad=1; }
exit $bad

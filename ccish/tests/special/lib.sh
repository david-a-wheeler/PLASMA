#!/bin/sh
# The library checks gcc can't be the oracle for (the xrun tests cover
# string.h and stdio): main(argc, argv) through lib/start.c, with more
# words than it keeps; lib/exit.c's exit from inside a call, under plvm
# (its marker and BRK: the unwind is for an Apple II, though init runs
# its save routine here too); the init it gets; and the link errors for
# main(argc, argv) without the start object and for exit without
# lib/exit.
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

# exit from inside a call stops with its status, after what was printed;
# under plvm, through exit's marker, then the BRK, whose message plvm
# prints on stderr. Statuses: 3; -1, which is 255; and 0. The second
# prints text that doesn't end its line before exit's marker. (Its
# BRK is the 0 byte at __exitcap + BRK_AT, lib/exit.c.)
exit_run() {
    src_obj "$1" "#include <stdlib.h>
void deep(int n) { if (n) deep(n - 1); else exit($2); }
int main(void) { __puts(pstr(\"$3\")); deep(5); return 9; }" || bad=1
    ./ccrel -o "$D/$1" "$D/$1" "$L/exit" || bad=1
    out=$(run_args "$1")
    st=$?
    same "$st" "$4" "exit status for exit($2)"
    case "$out" in      # stderr's BRK message may come first
        *"$3"*) ;;
        *) echo "FAIL lib exit($2): output '$out'"; bad=1 ;;
    esac
    case "$out" in
        *BRK:*) ;;
        *) echo "FAIL lib exit($2): no BRK in '$out'"; bad=1 ;;
    esac
}
exit_run quit 3 before 3
exit_run quitm -1 partial 255
exit_run quit0 0 zero 0

# With lib/exit.c's exit, init sets TMP ($E7) to __exitcap's first
# routine (at data+0, the module's only data) and runs it right before
# main's call, and for a
# void main pushes the 0 that exit may replace.
init_has() {
    ./ccrel -l -o "$D/$1" "$D/$1" "$L/exit" >"$D/$1.lst" || bad=1
    sed -n '/^(init):/,/RET/p' "$D/$1.lst" | sed '1d; s/^  [0-9A-F]*  //' \
        | cut -c10- | sed 's/ \$[0-9A-F]* *; / /' | tr '\n' '|' >"$D/$1.init"
    same "$(cat "$D/$1.init")" "$2" "lib/exit's init for $1"
}
init_has quit 'LA data+0|DUP|SAW $00E7|ICAL|CALL main|ANDI 255|NEG|RET|'
src_obj vquit '#include <stdlib.h>
void main(void) { exit(4); }' || bad=1
init_has vquit 'LA data+0|DUP|SAW $00E7|ICAL|CN 0|CALL main|ANDI 255|NEG|RET|'

# The link errors: no start object, and no exit.
if ./ccrel -o "$D/nostart" "$D/args" "$L/putc" "$L/fputs" "$L/strlen" 2>"$D/nostart.err"; then
    echo "FAIL lib: main(argc, argv) linked without lib/start"; bad=1
fi
grep -qF "undefined reference to '__start'" "$D/nostart.err" \
    || { echo "FAIL lib: wrong message without lib/start:"; cat "$D/nostart.err"; bad=1; }
if ./ccrel -o "$D/noexit" "$D/quit" 2>"$D/noexit.err"; then
    echo "FAIL lib: exit linked without lib/exit"; bad=1
fi
grep -qF "undefined reference to 'exit'" "$D/noexit.err" \
    || { echo "FAIL lib: wrong message without an exit:"; cat "$D/noexit.err"; bad=1; }
exit $bad

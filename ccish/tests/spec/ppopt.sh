#!/bin/sh
# ccpp's options (approach.md, "ccpp design", step 4): -D name=text,
# -D name (1), -U, in order; -I dir (or -Idir) for <file>, looked for
# only there; and the predefined __CCISH__. ccish takes them too. The
# program prints "7 2 1 0 1".
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT/ppopt/inc"
printf '#define FROM_INC 1\n' >"$OUT/ppopt/inc/sys.h"
cat >"$OUT/ppopt/t.c" <<'EOF'
#include <sys.h>
#ifdef GONE
int gone = 1;
#endif
int main()
{
  __puti(SEVEN); __putc(' '); __puti(TWO); __putc(' '); __puti(ONE); __putc(' ');
#ifdef GONE
  __puti(9);
#else
  __puti(0);
#endif
  __putc(' '); __puti(__CCISH__ * FROM_INC); __putln();
  return 0;
}
EOF
if ./ccish -o "$OUT/ppopt/t" -D SEVEN=7 -DTWO=2 -DONE -D GONE -U GONE \
        -I "$OUT/ppopt/inc" "$OUT/ppopt/t.c" 2>"$OUT/ppopt/t.err" \
   && out=$(run_module "$OUT/ppopt" t) \
   && [ "$out" = "7 2 1 0 1" ] \
   && ./ccpp -o "$OUT/ppopt/u" -I"$OUT/ppopt/inc" -DSEVEN=7 -DTWO -DONE \
        "$OUT/ppopt/t.c" 2>>"$OUT/ppopt/t.err"; then
    :
else
    echo "FAIL ccpp/ccish: -D, -U and -I don't work as they should"
    sed 's/^/    /' "$OUT/ppopt/t.err"
    bad=1
fi
rm -f "$OUT/ppopt/u.i" "$OUT/ppopt/u.nam"
# <file> isn't looked for beside the including file, only in -I's.
cp "$OUT/ppopt/inc/sys.h" "$OUT/ppopt/sys.h"
if ./ccpp -o "$OUT/ppopt/v" "$OUT/ppopt/t.c" 2>"$OUT/ppopt/v.err" \
   || ! grep -qF "error 236: cannot open the #include file" \
        "$OUT/ppopt/v.err"; then
    echo "FAIL ccpp: found <sys.h> beside the including file, with no -I"
    bad=1
fi
# The macro table full (MAX_MACROS, 72): __CCISH__ is one, so the
# 72nd #define is one too many.
{
    i=1
    while [ $i -le 129 ]; do echo "#define M$i $i"; i=$((i + 1)); done
} >"$OUT/ppopt/many.c"
if ./ccpp -o "$OUT/ppopt/many" "$OUT/ppopt/many.c" 2>"$OUT/ppopt/many.err" \
   || ! grep -qF "many.c:72: fatal 244: too many macros" \
        "$OUT/ppopt/many.err"; then
    echo "FAIL ccpp: 129 macros, wanted 'too many macros' on line 72, got:"
    sed 's/^/    /' "$OUT/ppopt/many.err"
    bad=1
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
    bad=1
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
    :
else
    echo "FAIL ccpp -E: wanted, then got:"
    sed 's/^/    /' "$OUT/ppe/e.want"
    echo "    ---"
    sed 's/^/    /' "$OUT/ppe/e.got" "$OUT/ppe/e.err"
    bad=1
fi
# The small build has no -E.
if ./ccpp-small -E -o "$OUT/ppe/s" "$OUT/ppe/e.c" >/dev/null 2>&1; then
    echo "FAIL ccpp-small: accepted -E"
    bad=1
fi
exit $bad

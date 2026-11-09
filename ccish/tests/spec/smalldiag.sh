#!/bin/sh
# The small build's diagnostics (src/build.h, CCISH_DIAG): an error
# gives its file, line, number and text, the full build's message less
# its detail, except the few details error_d() and fatal_d() keep
# (diag.h: here the program's own text in #error and _Static_assert);
# the file is right inside a header too (the name is looked up only for
# the message, tokin.c); and there are no warnings, where the full
# build gives them. The ccish driver, which removes NAME.nam on an
# error, still names the header.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out/smalldiag
bad=0
rm -rf "$OUT"
mkdir -p "$OUT"
printf 'int main()\n{\n  return y;\n}\n' >"$OUT/a.c"
printf 'int f(void);\nint g(void) { return zz; }\n' >"$OUT/h.h"
printf '#include "h.h"\nint main()\n{\n  return 0;\n}\n' >"$OUT/b.c"
printf '#frobnicate\nint main(void) { return 0; }\n' >"$OUT/c.c"
printf '_Static_assert(0, "kept here");\nint main(void) { return 0; }\n' >"$OUT/s.c"
printf '#error stop here\nint main(void) { return 0; }\n' >"$OUT/e.c"

# $1 the source's tag, $2 the full build's message, $3 its detail.
check() {
    full=$(cd "$OUT" && { ../../../ccpp -o "f$1" "$1.c" \
           && ../../../cccomp "f$1"; } 2>&1)
    small=$(cd "$OUT" && { ../../../ccpp-small -o "s$1" "$1.c" \
            && ../../../cccomp-small "s$1"; } 2>&1)
    driver=$(cd "$OUT" && ../../../ccish -o "d$1" "$1.c" 2>&1)
    if [ "$full" != "$2 $3" ] || [ "$driver" != "$2 $3" ]; then
        echo "FAIL smalldiag $1: wanted '$2 $3' from the full build, got:"
        echo "    cccomp: $full"; echo "    ccish:  $driver"; bad=1
    fi
    if [ "$small" != "$2" ]; then
        echo "FAIL smalldiag $1: wanted '$2' from the small build, got:"
        echo "    $small"; bad=1
    fi
}
check a "a.c:3: error 64: undeclared identifier" "'y'"
check b "h.h:2: error 64: undeclared identifier" "'zz'"
check c "c.c:1: error 234: unknown preprocessor directive:" "#frobnicate"

# $1 the source's tag, $2 the message, the same in both builds, its
# detail kept.
check_kept() {
    full=$(cd "$OUT" && { ../../../ccpp -o "f$1" "$1.c" \
           && ../../../cccomp "f$1"; } 2>&1)
    small=$(cd "$OUT" && { ../../../ccpp-small -o "s$1" "$1.c" \
            && ../../../cccomp-small "s$1"; } 2>&1)
    if [ "$full" != "$2" ] || [ "$small" != "$2" ]; then
        echo "FAIL smalldiag $1: wanted '$2' from both builds, got:"
        echo "    full:  $full"; echo "    small: $small"; bad=1
    fi
}
check_kept s "s.c:1: error 323: static assertion failed: kept here"
check_kept e "e.c:1: error 258: #error: stop here"

# tests/run/const.c draws four warnings from the full build, none from
# the small one.
n=$(./ccpp -o "$OUT/fw" tests/run/const.c && ./cccomp "$OUT/fw" 2>&1 \
    | grep -c ': warning ')
if [ "$n" != 4 ]; then
    echo "FAIL smalldiag: wanted 4 warnings from the full build, got $n"; bad=1
fi
small=$(./ccpp-small -o "$OUT/sw" tests/run/const.c \
        && ./cccomp-small "$OUT/sw" 2>&1)
if [ -n "$small" ]; then
    echo "FAIL smalldiag: the small build warned:"
    printf '%s\n' "$small" | sed 's/^/    /'; bad=1
fi
[ $bad = 0 ] && rm -rf "$OUT"
exit $bad

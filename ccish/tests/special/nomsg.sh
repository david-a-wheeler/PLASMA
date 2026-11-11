#!/bin/sh
# Without the message file (ccish.msg), a message is just its numbers,
# and a keyword or operator in a message is its number (approach.md,
# "C2"). Here a copy of ccish is run with no message file beside it.
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT/nomsg"
cp "$CCISH" "$OUT/nomsg/ccish"
# A message is its numbers: 41 ("lvalue required as") with part 194
# ("left operand of assignment").
printf 'int main() { 1 = 2; return 0; }\n' > "$OUT/nomsg/t.c"
if "$OUT/nomsg/ccish" -o "$OUT/nomsg/t" "$OUT/nomsg/t.c" 2>"$OUT/nomsg/t.err"
then
    echo "FAIL no message file: compiled, but should have failed"; bad=1
elif grep -qF "t.c:1: error 41 (194):" "$OUT/nomsg/t.err"; then
    :
else
    echo "FAIL no message file: wanted 'error 41 (194):', got:"
    sed 's/^/    /' "$OUT/nomsg/t.err"; bad=1
fi
# A keyword in a message is its number (tok_desc(): 'while' is 289, the
# keywords being 262-301 in their codes' order, token.h).
printf 'int main() { return while; }\n' > "$OUT/nomsg/k.c"
if "$OUT/nomsg/ccish" -o "$OUT/nomsg/k" "$OUT/nomsg/k.c" 2>"$OUT/nomsg/k.err"
then
    echo "FAIL no message file: compiled, but should have failed"; bad=1
elif grep -qF "k.c:1: error 65: '289'" "$OUT/nomsg/k.err"; then
    :
else
    echo "FAIL no message file: wanted \"error 65: '289'\", got:"
    sed 's/^/    /' "$OUT/nomsg/k.err"; bad=1
fi
exit $bad

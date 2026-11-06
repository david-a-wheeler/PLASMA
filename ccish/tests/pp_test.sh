#!/bin/sh
# The ccpp test for tests/pp/$1.c: its token stream and name file must
# equal tests/pp/$1.tok and $1.nam, with the checksum filled in for the
# "ssss"/"ss ss" placeholders.
. "$(dirname "$0")/lib.sh"
name=$1
mkdir -p "$OUT/pp"
sum=$(nam_sum "tests/pp/$name.nam")
lo=$(echo "$sum" | cut -c3-4 | tr 'A-F' 'a-f')
hi=$(echo "$sum" | cut -c1-2 | tr 'A-F' 'a-f')
# The expected token stream and name file, with the checksum filled in.
sed "s/ssss/$sum/" "tests/pp/$name.nam" >"$OUT/pp/$name.nam.want"
sed -e 's/#.*//' -e "s/ss ss/$lo $hi/" "tests/pp/$name.tok" \
    | tr -s ' \t' '\n\n' | grep . >"$OUT/pp/$name.tok.want"
# The actuals, from ccpp.
if ! ./ccpp -o "$OUT/pp/$name" "tests/pp/$name.c" 2>"$OUT/pp/$name.err"; then
    echo "FAIL $name: ccpp failed:"; sed 's/^/    /' "$OUT/pp/$name.err"; exit 1
fi
if [ -s "$OUT/pp/$name.err" ]; then
    echo "FAIL $name: unexpected ccpp messages:"; sed 's/^/    /' "$OUT/pp/$name.err"; exit 1
fi
od -An -v -tx1 "$OUT/pp/$name.i" | tr -s ' \t' '\n\n' | grep . >"$OUT/pp/$name.tok.got"
ok=1
if ! cmp -s "$OUT/pp/$name.tok.want" "$OUT/pp/$name.tok.got"; then
    echo "FAIL $name: token stream differs (wanted, then got):"
    tr '\n' ' ' <"$OUT/pp/$name.tok.want" | fold -w 72 | sed 's/^/    /'; echo "    ---"
    tr '\n' ' ' <"$OUT/pp/$name.tok.got" | fold -w 72 | sed 's/^/    /'; echo
    ok=0
fi
if ! cmp -s "$OUT/pp/$name.nam.want" "$OUT/pp/$name.nam"; then
    echo "FAIL $name: name file differs:"
    diff "$OUT/pp/$name.nam.want" "$OUT/pp/$name.nam" | sed 's/^/    /'
    ok=0
fi
rm -f "$OUT/pp/$name.i" "$OUT/pp/$name.nam"
exit $((1 - ok))

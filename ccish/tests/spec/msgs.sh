#!/bin/sh
# Check the message file ccish.msg against the sources (approach.md,
# "C2 design"). Run from the ccomp directory; prints each problem and
# exits non-zero if there are any.
#   - Each line of ccish.msg is a number, a space and a text, and the
#     numbers are in ascending order (so each is used once).
#   - Each message number in the sources, written as
#     NUMBER /* "text" */ (the text may be split into several quoted
#     pieces, as a C string), has exactly the text the file gives it.
#   - Each number in the file is used somewhere in the sources, or is
#     in a range that a source's comment declares as "messages A-B"
#     (texts looked up by computed number: tok_desc()'s token names).
#   - No diagnostic call gives a message number without its comment.

set -u
MSG=ccish.msg
TMP=${TMPDIR:-/tmp}/check_msgs.$$
trap 'rm -f "$TMP"' EXIT

# Every NUMBER /* "text" */ in the sources, one per line as NUMBER TEXT,
# with the pieces joined and \" and \\ unescaped.
for f in src/*.c src/*.h; do
    tr '\n' ' ' < "$f" |
    grep -oE '[0-9]+ /\* "([^"\\]|\\.)*"( +"([^"\\]|\\.)*")* \*/' |
    sed -e 's/" *"//g' -e 's|^\([0-9]*\) /\* "|\1 |' -e 's|" \*/$||' \
        -e 's/\\\(["\\]\)/\1/g'
done > "$TMP"
# Each number in a declared range, as used, with no text to compare.
cat src/*.c src/*.h | tr '\n' ' ' | grep -oE 'messages [0-9]+-[0-9]+' |
    while read -r _ range; do
        i=${range%-*}
        while [ "$i" -le "${range#*-}" ]; do
            echo "$i @range"
            i=$((i + 1))
        done
    done >> "$TMP"

bad=0
# Numbers given in a diagnostic call with no comment after them.
for f in src/*.c; do
    if tr '\n' ' ' < "$f" |
       grep -qE '\b(error|error_in|fatal|warning|warning_in)\( *[0-9]+ *[,)]'
    then
        echo "$f: a message number without its /* \"text\" */ comment"
        bad=1
    fi
done

awk -v msgfile="$MSG" '
    BEGIN {
        prev = 0
        while ((getline line < msgfile) > 0) {
            sub(/\r$/, "", line)
            if (line !~ /^[0-9]+ ./) {
                print msgfile ": malformed line: " line
                bad = 1
                continue
            }
            n = substr(line, 1, index(line, " ") - 1) + 0
            if (n <= prev) {
                print msgfile ": " n " is not above " prev
                bad = 1
            }
            prev = n
            text[n] = substr(line, index(line, " ") + 1)
        }
    }
    {
        n = substr($0, 1, index($0, " ") - 1) + 0
        t = substr($0, index($0, " ") + 1)
        used[n] = 1
        if (t == "@range" && (n in text))
            next
        if (!(n in text)) {
            print "message " n " is used but not in " msgfile ": " t
            bad = 1
        } else if (text[n] != t) {
            print "message " n " comment differs from " msgfile ":"
            print "    file:    " text[n]
            print "    comment: " t
            bad = 1
        }
    }
    END {
        for (n in text)
            if (!(n in used)) {
                print "message " n " in " msgfile " is never used: " text[n]
                bad = 1
            }
        exit bad
    }' "$TMP" || bad=1
exit $bad

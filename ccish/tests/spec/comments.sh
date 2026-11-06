#!/bin/sh
# Check that every function definition in src/*.c has a comment right
# above it (AGENTS.md, "Code style"). Run from the ccomp directory;
# prints each function without one and exits non-zero if there are
# any. A definition is a line that starts with a letter and holds a
# "(", followed by a line that is just "{"; its comment must end on the
# line just before it ("*/"), or be a // line. Files whose names start
# with "," are the user's own scratch files, and aren't checked.

set -u
bad=0
for f in src/*.c; do
    case $(basename "$f") in
        ,*) continue ;;
    esac
    if ! awk -v f="$f" '
        $0 == "{" && prev ~ /^[a-zA-Z].*\(/ \
                && !(prev2 ~ /\*\/$/ || prev2 ~ /^\/\//) {
            print f ":" NR - 1 ": no comment above: " prev
            bad = 1
        }
        { prev2 = prev; prev = $0 }
        END { exit bad }' "$f"; then
        bad=1
    fi
done
exit $bad

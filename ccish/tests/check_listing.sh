#!/bin/sh
# Check the listing directives in source $1 against the listing $2. A
# directive is a comment line in the source:
#   // listing-has: TEXT     the listing has a line containing TEXT
#   // listing-lacks: TEXT   the listing has no line containing TEXT
#   // listing-count: N TEXT the listing has exactly N lines with TEXT
# Exits nonzero if any directive is violated.
set -u
src=$1 lst=$2
bad=0
while IFS= read -r line; do
    case "$line" in
    "// listing-has: "*)
        text=${line#"// listing-has: "}
        grep -qF -- "$text" "$lst" || { echo "    listing has no line with: $text"; bad=1; } ;;
    "// listing-lacks: "*)
        text=${line#"// listing-lacks: "}
        grep -qF -- "$text" "$lst" && { echo "    listing should not contain: $text"; bad=1; } ;;
    "// listing-count: "*)
        rest=${line#"// listing-count: "}
        n=${rest%% *}; text=${rest#* }
        c=$(grep -cF -- "$text" "$lst")
        [ "$c" -eq "$n" ] || { echo "    listing has $c lines with '$text', expected $n"; bad=1; } ;;
    esac
done < "$src"
exit $bad

# Shared helpers for the test scripts (sourced, not run). The Makefile is
# the source of truth for which tests run and when (a test's stamp depends
# on its input and the passes); the scripts here only DO one test or check.
set -u
CCISH=./ccish
PLVM=$(cd ../src && pwd)/plvm
OUT=tests/out

# Compile source $3 into the object of output name $2 (its .obj and
# .syms) with ccpp, cccomp, then ccgen; $1 is "" for the full build or
# "-small" for the small one, and $4 is an extra ccpp option ("-I inc"
# for the cross tests, which include ptest.h).
cc_obj() {
    ./ccpp$1 ${4:-} -o "$2" "$3" && ./cccomp$1 "$2" && ./ccgen$1 "$2"
}

# Compile source $2 into the IR file of output name $1 ($1.ir) with
# ccpp and cccomp.
cc_ir() {
    ./ccpp -o "$1" "$2" && ./cccomp "$1"
}

# The checksum of name file $1 (CCPP.md, "The checksum"): over the bytes
# of its lines before the blank line, newlines included, as 4 upper-case
# hex digits. Written apart from ccpp's code, so that an expected checksum
# never comes from the program under test.
nam_sum() {
    LC_ALL=C awk '
        BEGIN { for (i = 1; i < 256; i++) ord[sprintf("%c", i)] = i }
        $0 == "" { exit }
        {
            for (i = 1; i <= length($0); i++)
                h = (h * 31 + ord[substr($0, i, 1)]) % 65536
            h = (h * 31 + 10) % 65536
        }
        END { printf "%04X\n", h }' "$1"
}

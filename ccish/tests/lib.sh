# Shared helpers for the test scripts (sourced, not run). The Makefile is
# the source of truth for which tests run and when (a test's stamp depends
# on its input and the passes); the scripts here only DO one test or check.
set -u
# The directory of the passes to run, and the one the tests write in:
# the Makefile's PASSDIR and OUT, which it exports as CCISH_BIN and
# CCISH_OUT (the coverage targets set both: doc/coverage.md), or, for
# a script run by hand, the defaults, . and tests/out. Both are
# relative to the ccish directory, where the scripts run.
CCISH_DIR=${CCISH_BIN:-.}
CCISH=$CCISH_DIR/ccish
PLVM=$(cd ../src && pwd)/plvm
CCRUN=$(pwd)/tools/ccrun
OUT=${CCISH_OUT:-tests/out}

# Rewrite file $1 so that the paths under $OUT in it read as
# tests/out/..., as the expected messages and listing directives write
# them (they name the objects the test wrote), whatever OUT is.
canon_out() {
    [ "$OUT" = tests/out ] && return 0
    sed "s|$(printf '%s' "$OUT" | sed 's/[.]/\\./g')/|tests/out/|g" "$1" \
        >"$1.canon" && mv "$1.canon" "$1"
}

# Run module $2 from directory $1 under plvm, through tools/ccrun (with
# a 10-second timeout), and print its output, stdout and stderr, with
# the CRs taken out. Its status is ccrun's: main's exit status, or
# nonzero for a crash or a timeout. (The output goes to a file first,
# so that the status isn't lost in a pipe.) Use it as
# out=$(run_module dir mod) && [ "$out" = ... ], so a failing status
# fails the test.
run_module() {
    (cd "$1" && PLVM="$PLVM" "$CCRUN" -t 10 "$2" >"$2.run" 2>&1)
    set -- "$1" "$2" $?
    tr -d '\r' <"$1/$2.run"
    rm -f "$1/$2.run"
    return "$3"
}

# Compile source $3 into the object of output name $2 (its .obj and
# .syms) with ccpp, cccomp, then ccgen; $1 is "" for the full build or
# "-small" for the small one, and $4 is an extra ccpp option ("-I inc"
# for the cross tests, which include ptest.h).
cc_obj() {
    "$CCISH_DIR/ccpp"$1 ${4:-} -o "$2" "$3" && "$CCISH_DIR/cccomp"$1 "$2" && "$CCISH_DIR/ccgen"$1 "$2"
}

# Compile source $2 into the IR file of output name $1 ($1.ir) with
# ccpp and cccomp.
cc_ir() {
    "$CCISH_DIR/ccpp" -o "$1" "$2" && "$CCISH_DIR/cccomp" "$1"
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

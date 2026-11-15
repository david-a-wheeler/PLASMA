# What the tests cover

The line and branch coverage of the host's passes, from a chosen set
of the tests. The point is to find what no test covers, and add tests
for it. You need gcc (gcov reads its data) and gcovr.

## Run

    make cov-xrun    the cross tests (tests/xrun): the oracle is a host
                     compiler, so this is the most reliable set to
                     start from
    make cov-run     the run and gen tests: the expected outputs are
                     worked out by hand
    make cov-all     every kind, the bootstrap included (about 25
                     seconds with -j8)
    make coverage KINDS="xrun err"
                     any set of the kinds: run img gen xrun err pp
                     link boot

`-j` works, and is safe (see below). Each set's results are kept
apart, in tools/out/cov/SET, where SET is the kinds, sorted and joined
with `+` (e.g. `err+xrun`; cov-xrun's is `xrun`):

- `report.txt`: printed at the end of the run too. Two gcovr tables:
  each source's lines, how many ran and the line numbers that didn't;
  then the same for branches. A branch is gcov's (a jump gcc emitted),
  not a C `if`, so a line can have several.
- `html/index.html`: the annotated sources, each line's count and
  branches.
- `coverage.json`: gcovr's data, for another report
  (`gcovr -a tools/out/cov/SET/coverage.json ...`).

## How it works

The coverage build is the Makefile's own test rules, run again with
two variables changed: `PASSDIR`, where the passes are built
(tools/out/cov/bin), and `OUT`, where the tests and what they need
write (tools/out/cov/out). The passes there are built with gcc's
`--coverage`. So the coverage targets run exactly the regular checks,
with nothing to keep in step, and never touch the passes beside the
source or tests/out. The test scripts get the two directories from
the environment (`CCISH_BIN` and `CCISH_OUT`, which the Makefile
exports; tests/lib.sh). A script run by hand uses the defaults, `.`
and tests/out.

A few expected results name an output path (the link tests' messages
and listing directives say `tests/out/link/...`). tests/link_test.sh
rewrites the paths under `$OUT` to read `tests/out/` before comparing
(`canon_out`, tests/lib.sh), so the same expected files work wherever
`OUT` is.

Every run is fresh. It first builds everything the tests need (the
passes, the library, ccstat, XLOAD), then deletes the counters (the
`.gcda` files, beside the passes), then removes the set's stamps and
runs its tests, so the report counts exactly the set's tests, and not
the building of the library. Then gcovr reads the counters, merging
each source's counts over the programs it's compiled into (pexpr.c is
in both ccish and cccomp, for instance), and keeps only src/.

Parallel runs are safe: gcc's runtime merges a program's counts into
its `.gcda` under a file lock, so processes that finish together don't
lose updates. I checked it both ways: 400 copies of a small
`--coverage` program run at once gave exactly 400 runs' counts, and
`make cov-all` with `-j8` and without `-j` gave the same total,
225,660,744 line executions (2026-11-15).

Run one set at a time: all the sets share the instrumented passes and
their counters, so two runs at once would mix their counts.

## What it measures, and what it doesn't

- The full build, the passes the tests run: ccish (the driver), and
  ccpp, cccomp, ccgen and cclink, built without `-DCCISH_SMALL`. So
  the full build's own code, such as `CCISH_TEXT` (ccpp -E,
  ppout.c), `CCISH_LISTING` and `CCISH_LINT`, is measured when a test
  runs it.
- Not the small build, the one that goes to the Apple II. Its
  programs are built too (the tests' rules need them), but none of
  the kinds above runs them; the special scripts that do aren't part
  of a set. The small build's code is nearly a subset of the full
  build's, so this matters only for the code under `#ifdef
  CCISH_SMALL` or a feature macro's `#else`.
- Not the code ccish generates, nor the library: this is coverage of
  the compiler's C source, as the host compiles it.

# What the tests cover

The gcov (statement and branch) coverage of the host's passes, from
selected sets of the tests. The point is to find what no test covers,
and add tests for it.

## Run

    make cov-xrun    the cross tests (tests/xrun): the oracle is a host
                     compiler, and so the most reliable set to start
                     from
    make cov-run     the run and gen tests: the expected outputs are
                     worked out by hand
    make cov-all     every kind, including the bootstrap
    make coverage KINDS="xrun err"
                     another set; the kinds are run img gen xrun err
                     pp link boot, and the set's directory is named
                     for them, the kinds sorted and joined with +

The report is printed, and kept, separately for each set, in
tools/out/cov/<set>/report.txt; the sets never share results.

## What a report says

- Each source file's, and the total's, statement and branch coverage
  over the set's tests, merged over the passes the tests ran (a test
  that runs only ccpp can't cover what's in cccomp).
- The statements, and branches, no test of the set covers: the line,
  and, for the branches, (n of m), how many of the line's branches a
  test takes, so what a test would have to do. The branches are
  gcov's, the jumps the compiler emitted, not the C's ifs, and they
  are per source line, as gcov's data is.
- The merged annotated sources, one per source, in the set's
  annotated/: each line's executions, and the branches taken.

## How it works, and what's stale when

The passes are built instrumented (-fprofile-arcs
-ftest-coverage) in tools/out/cov/build (tools/covbuild.sh), each
program's sources in its own object directory, its .gcno beside each
.o, and ccish.msg beside the programs, as diag looks for it beside
them. A set's tests run against those passes with the regular
suite's checks; the counters, the .gcda, are wiped before the run, as
they accumulate across runs; and gcov's data, in the set's gcov/, is
merged by tools/coverage.py into the report.

A set is stale, and so its counters wiped, its tests re-run, and its
report rebuilt, when the passes' sources, its test files, or anything
its tests depend on changes. A stamp can't see a file leave its
prerequisites, so the set's membership is also re-found into its
tests.list every time, and its per-test files are removed then too,
as a test's files would otherwise outlive the test.

Run one set at a time: the passes share the counters, and two sets
at once would mix their coverage. The set's tests are chained, each
after its predecessors, so they run one at a time even with -j, as
parallel .gcda writes can lose updates.

CC must be gcc, as gcov reads its data.

## Notes

- The coverage is of the host's passes, all five, as the tests
  exercise them: ccish (the driver, the small build), and ccpp,
  cccomp, ccgen and cclink.
- The small build has no text, no -E, no listings' text: ppout and
  the text's parts are built only in the full build (CCISH_TEXT,
  src/build.h), and so are not covered by the tests.
- The xrun set is the place to start adding tests: its oracle is a
  host compiler.

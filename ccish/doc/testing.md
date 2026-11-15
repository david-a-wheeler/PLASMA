# Improving ccish's tests: instructions for an agent

These are instructions for you, an AI agent (or a person), asked to
improve ccish's test suite. They can be given again and again, to many
agents in turn; each run should leave the suite a little better at
catching defects. Read AGENTS.md first, especially "Testing": its
rules apply here, and this file adds to them. The reference material
lives elsewhere, so this file points to it rather than repeating it:

- AGENTS.md, "Testing": the test kinds, their files, the listing
  directives (`// listing-has:` and the rest), and the rules for
  expected results.
- doc/coverage.md: the coverage targets, their reports, and what they
  do and don't measure.
- inc/ptest.h: the contract an xrun (cross) test must keep.

## The goal

The purpose of the test suite is to make it highly likely that if
ccish passes its tests, it's completely correct.

We use statement and branch coverage to find areas where testing is
inadequate. We *do* want high coverage, but high coverage is *not* the
goal: the goal is good tests that detect any plausible defect.
Coverage is a convenient proxy for finding weak areas. When coverage
is lacking, don't write tests *merely* to raise it; write tests that
thoroughly check the area. Coverage can only show that code that
exists was run, not that code is missing: we want tests that would
fail if the code for some case were missing, not only tests that run
the code that's there. Arrange each test so that incorrect code would
be likely to change its result.

## The loop

1. **Find a weak area.** Run `make -j8 cov-all` (about 25 seconds), or
   `make cov-xrun` for the cross tests alone, and read
   tools/out/cov/SET/report.txt: each source's line and branch
   coverage, and the lines no test runs. html/index.html shows each
   line's count and branches. Pick by risk (below), not by the lowest
   percentage.
2. **Read the code** you're about to test, and the design behind it
   (the file's header comment, and approach.md's section on it).
   Work out what it must do in every case, including the cases it
   doesn't handle yet: those are the missing code coverage can't
   show.
3. **Write the test first, with its expected result worked out by
   hand** from C semantics (C11) and ccish's documented differences
   (doc/ccish-guide.md), never copied from ccish's output. An `int`
   is 16 bits: 65432 prints as -104. For an xrun test, the host
   compiler is the oracle, but check its output makes sense too.
4. **Run it** through make (`make -j8`; every stale test runs), and
   see whether it really passed.
5. **Check that it can fail.** Break the code the test is aimed at:
   flip a comparison, drop a case, change a constant, delete a call.
   Rebuild, run the test, and confirm it fails; then put the code back
   exactly (`git diff` must show nothing in src/). A test that still
   passes against broken code checks nothing. Do this for every new
   test that targets compiler logic, and say in the commit message
   what you broke and that the test caught it.
6. **Commit it now** (below), before you start the next test. Then
   go back to step 1 or 3.

**Commit each test as soon as it passes and you've checked that it
can fail; don't save tests up for one commit at the end.** The
session can end at any moment (the user sometimes has to shut down),
and anything uncommitted is lost. A finished, committed test is never
lost; at worst, the one in progress is.

## When a test fails

**Never change an expected result to match ccish's output.** If a new
test fails, either your expected result is wrong or ccish is.

- Recheck your expected result against the C standard, and with gcc
  where the program is portable (compile it with `-Wall -Wextra
  -funsigned-char -fwrapv` and run it). Watch 16-bit overflow, char
  being unsigned, and C's unspecified orders of evaluation
  (doc/ccish-guide.md, section 6.7): a test must not depend on them.
- If ccish is wrong, you've found a bug: the best outcome of this
  work. Commit the failing test and the fix together, as one tested
  step, with `make test` and `make bootstrap` passing (the fix
  changes the compiler). If the fix needs a design decision, or would
  cost the Apple II build memory, stop and report the bug with the
  failing test instead (AGENTS.md: discuss design before coding).
- If the test fails because of a ccish limit (doc/limits.md), it's
  not a bug: shrink the test.

## Choosing a test kind

| What you're testing | Use |
|---|---|
| What a correct program does | **xrun** (tests/xrun), where inc/ptest.h's contract allows: gcc and clang are well tested, so they're good oracles |
| What a correct program does, where xrun can't (ccish's own features, such as `pstr` and the `__cmd_` functions; `sizeof`; 16-bit wraparound you want to see directly) | **run** (tests/run), with the expected output worked out by hand |
| Generated code: an optimization or a peephole pattern | **run** with listing directives (`// listing-count: N TEXT`, `// listing-lacks: TEXT`), or **gen** (tests/gen, `ccish -m`'s output) |
| A diagnostic | **errors** (tests/errors): the program must fail with the message |
| The preprocessor | **pp** (tests/pp) for the token stream and name file; xrun or run for what a program means |
| Several source files linked together | **link** (tests/link) |
| XLOAD images | **img**: every run test also runs as an image, automatically |

Where practical and effective, prefer xrun. It isn't always either:
when a behavior can't be shown by a portable program that prints, use
run, or another kind; that's fine, as long as the expected result is
worked out by hand.

Notes on each:

- **xrun.** Read inc/ptest.h's contract first. The usual traps: every
  value must stay within 16 bits (pass a deliberate wraparound
  through `w16()`); char is unsigned on both; no `sizeof` except
  element counts; the host build uses `-Werror`, so a comma operand
  or a statement with no effect needs a `(void)`. Each test needs a
  `NAME.flags` file, empty if it adds no host flags. `make xref`
  writes `NAME.ref` with the host compiler; commit the `.c`, `.flags`
  and `.ref` together.
- **Generated code.** The listing directives are targeted: they check
  the ops that matter and nothing else, so prefer them to gen, whose
  `.exp` is the whole of `ccish -m`'s output and changes with any
  unrelated improvement. Count exactly, and say in a comment where
  each counted op comes from. Test both sides of an optimization:
  where it must apply (with exact counts) and where it must not (a
  separate test with `listing-lacks`), each with output that would
  differ if the optimization went wrong. Write the test and its
  expected result first, then run it; if it doesn't pass, find out
  why before changing anything.
- **errors.** These run with the full build only. The checks that
  only reject invalid programs (`CCISH_LINT`) aren't in the small
  build (approach.md decision 44), so don't expect the small build to
  report them. The `.stderr.expected` text must appear in the
  message; keep it to the message itself, not its line number,
  unless the line is what you're testing.
- **Limits.** A function has at most `ENTRY_LABEL` labels
  (doc/limits.md), and every loop takes 4: split a test with many
  loops into functions.

## Where to look first

Rank by what an undetected defect would cost:

1. **Silent wrong code**: a correct program that compiles but runs
   wrong. That's the code generator (peep.c, irloop.c, lower.c,
   emit.c, irin.c) and the parser's folding and code choices
   (pexpr.c, parse.c, pstmt.c). These matter most: nothing else
   catches them.
2. **Wrong objects or modules**: link.c, cclink's records, XLOAD
   images.
3. **Wrong or missing diagnostics** for invalid programs: ptop.c,
   pexpr.c's checks, diag.c.
4. **Crashes or hangs on bad input**: the passes' file handling
   (bio.c, tokin.c, nam.c).

Uncovered code a valid test can't reach may be dead. Don't twist a
test to reach it: report it, as a candidate for deletion, since every
byte matters on the Apple II (doc/diet.md).

## Committing

Follow AGENTS.md's commit rules: stage files by explicit path, never
commit a file whose name starts with a comma, never push, and end the
message with the `Assisted-by:` and `Signed-off-by:` lines.

**One commit per test**, as soon as it's done (a few closely related
test files may share a commit, if you wrote them together). **A test
that found a bug is committed with its fix, in one commit**, as soon
as the fix passes: never commit a failing test, since `make test` must
pass at every commit. If you stop to report a bug instead of fixing
it, keep the failing test and its expected result in a directory
whose name starts with a comma, outside tests/ (e.g. `,bugs/`, beside
the Makefile), and name it in your report. Not in a tests/ directory,
even with a comma name: the Makefile's wildcards (`tests/run/*.c` and
the rest) would run it, and `make test` would fail. A comma name
keeps git from taking it by accident.

Each commit message says:

- what the test checks, and why those cases;
- for a test aimed at compiler logic, what you broke to check it
  could fail, and that it did;
- that `make test` passes (and `make bootstrap`, if you changed the
  compiler);
- the coverage of the area before and after (its lines and branches,
  from report.txt), as numbers. `make -j8 cov-all` takes about 25
  seconds, so measure for each commit when that's practical; if not,
  give the numbers in the commit that finishes an area.

## What we've learned

Add to this list what you find: areas that resist testing and why,
and traps that cost you time.

- Coverage of the small build isn't measured (doc/coverage.md). Code
  only under `CCISH_SMALL`, or a feature macro's `#else`, is checked
  by tests/special/small.sh, which compiles every run, xrun, gen and
  link test's source with both builds and requires the same token
  streams, name files, objects and modules; a new test of those kinds
  adds to that check for free.
- The samples (samples/*.c) aren't tests of correctness: they only
  check that the two builds agree, so their coverage isn't counted.

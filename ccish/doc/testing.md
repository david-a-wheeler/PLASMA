# Improve test suite

Please improve the test suite. The purpose of the test suite is to make highly
likely that if ccish passes its test suite, it's completely correct.

We use statement and branch coverage as a way to detect areas where there is
inadequate testing. While we *do* want high coverage, the goal is *NOT* high coverage,
the goal is good tests that detect any plausible defect. Coverage is simply a
convenient proxy to help us identify problematic areas. When coverage is lacking,
create tests not MERELY to improve coverage, but to thoroughly test the area
not adequately tested. In particular, coverage testing can only detect the
*presence* of code, not its absence; we want the tests to detect code for
some case is missing, not merely if the code that exists is tested.
Try to arrange the tests to increase the likelihood that incorrect code
would be detected.

Where practical and effective, we prefer tests to use the "xrun" approach, because gcc and clang
are well tested and thus good oracles.
Remember that there are differences, e.g., their "int" will have a different size.
However, xrun is *not*
always practical or effective.
If something can't be cross-compiled and shown by printing this is terrible.

For example, when testing peephole optimizations, typically you should use
`ccish -m`. Create the test .c file and expected result first, then run the test (via make)
and see if it really passed. If it didn't investigate why.

#!/bin/sh
# The compiler bootstrap test: the compiler compiles itself, twice,
# under the VM ($BOOTSTRAP_VM), and stage 2's and stage 3's modules
# must be byte-for-byte the same. Run by `make bootstrap` (or
# `make full-test`), not by the regular suite.
#
# Not written yet. The first attempt ran all of ccish in one
# invocation, which can't work; the stages will build the four pass
# programs, one source file per invocation, then link each with
# ccrel. That needs the library and module imports first. See
# doc/bootstrap-test.md (R1, and the implementation order).

echo "bootstrap: not implemented yet; see doc/bootstrap-test.md"
exit 1

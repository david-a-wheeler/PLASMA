#!/usr/bin/env python3
"""Count the machine ops in listings, to find where better code pays.

Usage: tools/opstats.py [-p PROGRAM] [-o] [-n N] [-l LENGTH] [FILE...]

Reads listings tools/listings.py wrote (tools/out/lst/NAME.lst): by
default all of them, or with -p, the files of one program (ccpp,
cccomp, ccgen or ccrel, from the Makefile through measure.py), or the
FILEs given. Prints how often each op appears, and the most frequent
runs of 2 up to LENGTH (default 4) ops: the places where a peephole
pattern or better code generation would pay most. A run never spans a
label or two functions, since code can be entered there. Ops are by
mnemonic; -o keeps their operands (LLW 0 and LLW 2 then differ), and
-n gives how many of each to print (default 20).

These are counts of ops, not bytes: an op is 1 to 3 bytes (CN 1, CB 2,
LA 3). The counts are static (how often an op is written); for how
often one runs, see tools/opcount.py.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure                              # noqa: E402 (its source lists)

LST = os.path.join(measure.OUT, "lst")


def runs_of(path, operands):
    """The listing at path as runs of ops: [[op, ...], ...], a run
    ending at each label and function."""
    runs, cur = [], []
    with open(path) as f:
        for line in f:
            if line.startswith(";;;; ") or line.startswith("    ; label"):
                if cur:
                    runs.append(cur)
                cur = []
                continue
            m = re.match(r"^    ([A-Z][A-Z0-9]*)(.*)$", line.rstrip("\n"))
            if m:
                cur.append((m.group(1) + m.group(2)).strip() if operands
                           else m.group(1))
    if cur:
        runs.append(cur)
    return runs


def main():
    args = sys.argv[1:]
    operands, n, length, prog, files = False, 20, 4, None, []
    while args:
        a = args.pop(0)
        if a == "-o":
            operands = True
        elif a == "-n" and args:
            n = int(args.pop(0))
        elif a == "-l" and args:
            length = int(args.pop(0))
        elif a == "-p" and args:
            prog = args.pop(0)
        elif a.startswith("-"):
            print(__doc__.split("\n\n")[1])
            return 2
        else:
            files.append(a)
    if prog:
        names = dict(measure.PROGRAMS).get(prog)
        if names is None:
            print("opstats: no program %s" % prog)
            return 2
        files = [os.path.join(LST, x + ".lst") for x in names]
    elif not files:
        files = sorted(os.path.join(LST, f) for f in os.listdir(LST)
                       if f.endswith(".lst"))
    missing = [f for f in files if not os.path.exists(f)]
    if missing:
        print("opstats: no %s (run tools/listings.py)" % missing[0])
        return 2
    runs = [r for f in files for r in runs_of(f, operands)]
    ops = collections.Counter(op for r in runs for op in r)
    total = sum(ops.values())
    print("%d ops in %d files" % (total, len(files)))
    print("\nops:")
    for op, c in ops.most_common(n):
        print("  %6d %5.1f%%  %s" % (c, 100.0 * c / total, op))
    for k in range(2, length + 1):
        seqs = collections.Counter(tuple(r[i:i + k]) for r in runs
                                   for i in range(len(r) - k + 1))
        print("\nruns of %d:" % k)
        for s, c in seqs.most_common(n):
            print("  %6d  %s" % (c, " ; ".join(s)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

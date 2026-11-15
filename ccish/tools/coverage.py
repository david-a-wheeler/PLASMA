#!/usr/bin/env python3
"""The report of a coverage set (doc/coverage.md): the statements and
branches the set's tests do and don't cover.

The Makefile's coverage targets build the passes instrumented
(-fprofile-arcs -ftest-coverage), run a set's tests against them, and
run gcov, which writes one .gcov.json.gz per source in the set's
gcov/<prog>/, for each program that ran. The JSON gives, per source,
each line's count and, for the line's branches, each branch's count
and the blocks it goes from and to.

A source is compiled into several programs (parse.c into cccomp and
ccish), with a data file per program; this merges them per source: a
statement is covered if any program's run executed it, a branch if any
took it (the counts are summed, a branch identified by its blocks).
Only the compiler's own sources (src/) are reported: gcov attributes
inlined system headers too, which are not the compiler's code.

The report goes to stdout: each source's and the total's statement and
branch coverage, and the statements and branches no test of the set
covers, the place to start adding tests. The annotated sources, merged
over the programs, go to the set's annotated/ directory, one
gcov-style file per source.

Usage: tools/coverage.py SET SETDIR
"""
import gzip
import json
import os
import sys


def load(setdir):
    """Merge the set's gcov JSONs.

    Returns (progs, files): the programs that ran, and, per source
    path, {"lines": {number: {"count": n, "branches":
    {(from_block, to_block): n}}}, "funcs": {name: n}}, each line's
    and branch's count summed over the programs.
    """
    gdir = os.path.join(setdir, "gcov")
    progs = sorted(os.listdir(gdir)) if os.path.isdir(gdir) else []
    files = {}
    for prog in progs:
        d = os.path.join(gdir, prog)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".gcov.json.gz"):
                continue
            with gzip.open(os.path.join(d, fn), "rt") as f:
                doc = json.load(f)
            for src in doc.get("files", []):
                path = src["file"]
                if not path.startswith("src/"):
                    continue
                e = files.setdefault(path, {"lines": {}, "funcs": {}})
                for ln in src.get("lines", []):
                    le = e["lines"].setdefault(
                        ln["line_number"], {"count": 0, "branches": {}})
                    le["count"] += ln.get("count", 0)
                    for b in ln.get("branches", []):
                        key = (b.get("source_block_id"),
                               b.get("destination_block_id"))
                        le["branches"][key] = \
                            le["branches"].get(key, 0) + b.get("count", 0)
                for fu in src.get("functions", []):
                    e["funcs"][fu["name"]] = max(
                        e["funcs"].get(fu["name"], 0),
                        fu.get("execution_count", 0))
    return progs, files


def ranges(nums):
    """[1, 2, 5, 6, 7] -> "1-2, 5-7" """
    out = []
    start = prev = None
    for n in sorted(nums):
        if start is None:
            start = prev = n
        elif n == prev + 1:
            prev = n
        else:
            out.append("%d" % start if start == prev else "%d-%d" % (start, prev))
            start = prev = n
    if start is not None:
        out.append("%d" % start if start == prev else "%d-%d" % (start, prev))
    return ", ".join(out)


def pct(a, b):
    return 100.0 if b == 0 else 100.0 * a / b


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: coverage.py SET SETDIR")
    setname, setdir = sys.argv[1], sys.argv[2]
    progs, files = load(setdir)

    # The set's tests: one stamp per test, in its kind's directory.
    tests = 0
    for d in os.listdir(setdir):
        sub = os.path.join(setdir, d)
        if os.path.isdir(sub):
            tests += len([f for f in os.listdir(sub) if f.endswith(".stamp")])

    rows = []
    stmt = stmt_c = br = br_c = fu = fu_c = 0
    for path in sorted(files):
        e = files[path]
        s = len(e["lines"])
        s_c = sum(1 for l in e["lines"].values() if l["count"])
        b = sum(len(l["branches"]) for l in e["lines"].values())
        b_c = sum(1 for l in e["lines"].values()
                  for n in l["branches"].values() if n)
        f = len(e["funcs"])
        f_c = sum(1 for n in e["funcs"].values() if n)
        stmt += s
        stmt_c += s_c
        br += b
        br_c += b_c
        fu += f
        fu_c += f_c
        rows.append((path, s_c, s, b_c, b))

    print('ccish coverage: the "%s" set' % setname)
    print("%d tests; programs that ran: %s"
          % (tests, ", ".join(progs) if progs else "none"))
    print()
    print("%-18s %10s %9s" % ("file", "statement", "branch"))
    for path, s_c, s, b_c, b in rows:
        print("%-18s %9.1f%% %8.1f%%" % (path, pct(s_c, s), pct(b_c, b)))
    print("%-18s %9.1f%% %8.1f%%"
          % ("TOTAL", pct(stmt_c, stmt), pct(br_c, br)))
    print("  %d/%d statements, %d/%d branches, %d/%d functions"
          % (stmt_c, stmt, br_c, br, fu_c, fu))

    print()
    print("Statements no test of the set covers:")
    any_ = False
    for path in sorted(files):
        miss = [n for n, l in files[path]["lines"].items() if not l["count"]]
        if miss:
            print("  %s: %s" % (path, ranges(miss)))
            any_ = True
    if not any_:
        print("  none")

    print()
    print("Branches no test of the set takes:")
    any_ = False
    for path in sorted(files):
        miss = []
        for n in sorted(files[path]["lines"]):
            l = files[path]["lines"][n]
            m = sum(1 for c in l["branches"].values() if not c)
            if m:
                miss.append("%d (%d of %d)" % (n, m, len(l["branches"])))
        if miss:
            print("  %s: %s" % (path, ", ".join(miss)))
            any_ = True
    if not any_:
        print("  none")

    # The annotated sources, merged over the programs, one per source.
    for path in sorted(files):
        e = files[path]
        out = os.path.join(setdir, "annotated", path + ".gcov")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(path) as f:
            text = f.read().splitlines()
        with open(out, "w") as f:
            for i, line in enumerate(text, 1):
                l = e["lines"].get(i)
                if l is None:
                    f.write("       -: %4d: %s\n" % (i, line))
                else:
                    f.write("%8d: %4d: %s\n" % (l["count"], i, line))
                    for j, c in enumerate(sorted(l["branches"].values())):
                        f.write("branch  %d %s\n"
                                % (j, "taken %d" % c if c else "never executed"))

    print()
    print("Annotated sources, merged over the programs that ran: "
          + os.path.join(setdir, "annotated"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Count, in loops, the accesses to globals moved to the heap.

Usage: tools/loops.py   (after tools/measure.py, which writes the
                         listings it reads, tools/out/PROGRAM.lst)

A global moved to the heap (approach.md, "Large zero-filled data on the
heap") is reached through its pointer word: each access is an LAW of
that word, about 24 cycles more than the LA it replaced. This reports,
for each Apple II program measure.py links, where those LAWs are, by
global and by function, counting those inside loops: a static count,
which shows where hoisting a table's address out of a loop could pay
(approach.md, "Hoisting moved globals' addresses"), not how often each
loop runs.

From each ccrel -l listing it takes:
  - the moved globals, from its "; heap global N: NAME, SIZE bytes"
    lines (the full build's ccrel writes them);
  - their pointer words, from the init function's DAW lines, which
    store them in the same order;
  - the loops: each backward branch is one, from its target to itself;
    an LAW inside n of them has loop depth n.
A static function that the listing names only by its address
(static@$XXXX, when its name didn't fit) is named from its object's
.syms file.
"""
import os
import re
import struct
import sys
from collections import Counter, defaultdict

sys.dont_write_bytecode = True     # no tools/__pycache__
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure import OUT, PROGRAMS  # noqa: E402

INSN = re.compile(r"^  ([0-9A-F]{4})  (?:[0-9A-F]{2} ?)+\s+(\S+)\s*(.*)$")
TO = re.compile(r"; to ([0-9A-F]{4})")
HEAP = re.compile(r"^; heap global (\d+): (\S+), (\d+) bytes$")
OBJECT = re.compile(r"^object (.*):$")


def static_names(starts):
    """Map the load address of each function the objects define to its
    name, given each object's first code address (by file name)."""
    names = {}
    for obj, start in starts.items():
        try:
            with open(os.path.join(OUT, obj + ".syms"), "rb") as f:
                d = f.read()
        except OSError:
            continue
        nfsyms = struct.unpack_from("<H", d, 11)[0]
        p = 15                      # after the header (OBJECT.md)
        for _ in range(nfsyms):
            flags, _, off, _, n = struct.unpack_from("<BBHHB", d, p)
            if flags & 1:
                names[start + off] = d[p + 7:p + 7 + n].decode()
            p += 7 + n
    return names


def read_listing(path):
    """The moved globals (number -> (name, size)) and the instructions
    ((address, function, op, operands), in order) of a listing."""
    moved, insns, starts = {}, [], {}
    func = obj = None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            m = HEAP.match(line)
            if m:
                moved[int(m.group(1))] = (m.group(2), int(m.group(3)))
                continue
            m = OBJECT.match(line)
            if m:
                obj = os.path.basename(m.group(1))
                continue
            if line and not line.startswith((" ", ";")) \
                    and line.endswith(":"):
                func = line[:-1]
                continue
            m = INSN.match(line)
            if m:
                addr = int(m.group(1), 16)
                if obj is not None and obj not in starts:
                    starts[obj] = addr
                insns.append((addr, func, m.group(2), m.group(3)))
    names = static_names(starts)
    fix = re.compile(r"^static@\$([0-9A-F]{4})$")
    for i, (addr, func, op, args) in enumerate(insns):
        m = fix.match(func or "")
        if m and int(m.group(1), 16) in names:
            insns[i] = (addr, names[int(m.group(1), 16)], op, args)
    return moved, insns


def report(prog, path):
    """Print the report for one program's listing."""
    moved, insns = read_listing(path)
    words = [int(args.split()[0][1:], 16) for _, f, op, args in insns
             if f == "(init)" and op == "DAW"]
    word_of = {w: moved.get(i, ("?", 0))[0] for i, w in enumerate(words)}
    loops = []
    for addr, _, op, args in insns:
        m = TO.search(args)
        if m and op.startswith("BR") and int(m.group(1), 16) < addr:
            loops.append((int(m.group(1), 16), addr))
    total, inloop = Counter(), Counter()
    byfunc, deepest = defaultdict(Counter), Counter()
    for addr, func, op, args in insns:
        if func == "(init)" or op != "LAW":
            continue
        name = word_of.get(int(args.split()[0][1:], 16))
        if name is None:
            continue                # an ordinary global
        total[name] += 1
        depth = sum(1 for s, e in loops if s <= addr <= e)
        if depth:
            inloop[name] += 1
            byfunc[func][name] += 1
            deepest[func, name] = max(deepest[func, name], depth)
    print("%s: %d moved globals; %d accesses, %d of them in loops"
          % (prog, len(moved), sum(total.values()), sum(inloop.values())))
    print("  %-20s %6s %5s %9s" % ("global", "bytes", "all", "in loops"))
    for i in sorted(moved):
        name, size = moved[i]
        print("  %-20s %6d %5d %9d" % (name, size, total[name],
                                       inloop[name]))
    if byfunc:
        print("  in loops, by function (accesses, deepest loop nesting):")
    for n, func, c in sorted(((sum(c.values()), f, c)
                              for f, c in byfunc.items()),
                             key=lambda r: (-r[0], r[1])):
        print("    %-22s %3d  %s" % (func, n, ", ".join(
            "%s %d (depth %d)" % (t, k, deepest[func, t])
            for t, k in c.most_common())))


def main():
    missing = 0
    for prog, _ in PROGRAMS:
        path = os.path.join(OUT, prog + ".lst")
        if not os.path.exists(path):
            print("%s: no listing (run tools/measure.py -full first)" % prog)
            missing += 1
            continue
        report(prog, path)
        print()
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())

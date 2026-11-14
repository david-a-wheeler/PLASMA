#!/usr/bin/env python3
"""Each function's code size, op count and frame, from cclink -v listings.

Usage: tools/fsize.py LISTING [FUNCTION...]
       tools/fsize.py -f LISTING
       tools/fsize.py -c OLD NEW

The first form prints each function's bytes, ops and frame bytes (all
of them, by name, or the ones named); -f prints them all, the largest
frame first (ties by name), for finding what fills the frame stack.
The third compares two listings (say, OLD from a git worktree of an
earlier commit, NEW from this tree, both from tools/measure.py's
tools/out/PROGRAM.lst): each function whose size, op count or frame
differs, with the change, then the total bytes. A function's frame is
its ENTER's size when ENTER is its first op, as tools/measure.py takes
it (0 with no ENTER: a function without a frame).

In a listing, each "name:" line starts a function, and its size is its
address range. In tools/out/PROGRAM.lst, a static function whose name
didn't fit is named static@$ADDR, its load address, which shifts
between builds; -c leaves those out. tools/out/PROGRAM.named.lst, which
tools/measure.py links with room for every name, names them all: use
it for -f and -c.
"""
import re
import sys

LABEL = re.compile(r"^([A-Za-z_@$0-9()]+):$")
INSN = re.compile(r"^  ([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+)")


ENTER = re.compile(r"\bENTER (\d+),\d+")


def read(path):
    """{function: (bytes, ops, frame)} for the listing at path."""
    funcs, cur, start, last, ops, frame = {}, None, None, None, 0, 0

    def close():
        if cur is not None and start is not None:
            funcs[cur] = (last - start, ops, frame)

    with open(path) as f:
        for line in f:
            m = LABEL.match(line.rstrip())
            if m:
                close()
                cur, start, ops, frame = m.group(1), None, 0, 0
                continue
            m = INSN.match(line)
            if m and cur is not None:
                at = int(m.group(1), 16)
                if start is None:
                    start = at
                    e = ENTER.search(line)
                    if e:
                        frame = int(e.group(1))
                last = at + len(m.group(2).split())
                ops += 1
    close()
    return funcs


def main():
    args = sys.argv[1:]
    if len(args) == 3 and args[0] == "-c":
        old, new = read(args[1]), read(args[2])
        delta = 0
        for name in sorted(set(old) | set(new)):
            if name.startswith("static@"):
                continue
            a, b = old.get(name, (0, 0, 0)), new.get(name, (0, 0, 0))
            if a != b:
                delta += b[0] - a[0]
                print("%-24s %5d -> %5d bytes (%+d)   %4d -> %4d ops"
                      "   frame %3d -> %3d (%+d)" %
                      (name, a[0], b[0], b[0] - a[0], a[1], b[1],
                       a[2], b[2], b[2] - a[2]))
        print("named functions: %+d bytes" % delta)
        return 0
    if len(args) == 2 and args[0] == "-f":
        funcs = read(args[1])
        names = sorted(funcs, key=lambda n: (-funcs[n][2], n))
    elif args and not args[0].startswith("-"):
        funcs = read(args[0])
        names = args[1:] or sorted(funcs)
    else:
        sys.exit(__doc__)
    for name in names:
        size, ops, frame = funcs.get(name, (None, None, None))
        print("%-24s %5s bytes %4s ops %4s frame" % (name, size, ops, frame))
    return 0


if __name__ == "__main__":
    sys.exit(main())

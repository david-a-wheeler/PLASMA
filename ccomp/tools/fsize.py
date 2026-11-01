#!/usr/bin/env python3
"""Each function's code size, and its op count, from ccrel -l listings.

Usage: tools/fsize.py LISTING [FUNCTION...]
       tools/fsize.py -c OLD NEW

The first form prints each function's bytes and ops (all of them, or
the ones named). The second compares two listings (say, OLD from a git
worktree of an earlier commit, NEW from this tree, both from
tools/measure.py's tools/out/PROGRAM.lst): each function whose size or
op count differs, with the change, then the total. In a listing, each
"name:" line starts a function, and its size is its address range. A
static function whose name didn't fit is named static@$ADDR, its load
address, which shifts between builds; those are left out of -c (compare
measure.py's table, by file, instead).
"""
import re
import sys

LABEL = re.compile(r"^([A-Za-z_@$0-9()]+):$")
INSN = re.compile(r"^  ([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+)")


def read(path):
    """{function: (bytes, ops)} for the listing at path."""
    funcs, cur, start, last, ops = {}, None, None, None, 0

    def close():
        if cur is not None and start is not None:
            funcs[cur] = (last - start, ops)

    with open(path) as f:
        for line in f:
            m = LABEL.match(line.rstrip())
            if m:
                close()
                cur, start, ops = m.group(1), None, 0
                continue
            m = INSN.match(line)
            if m and cur is not None:
                at = int(m.group(1), 16)
                if start is None:
                    start = at
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
            a, b = old.get(name, (0, 0)), new.get(name, (0, 0))
            if a != b:
                delta += b[0] - a[0]
                print("%-24s %5d -> %5d bytes (%+d)   %4d -> %4d ops" %
                      (name, a[0], b[0], b[0] - a[0], a[1], b[1]))
        print("named functions: %+d bytes" % delta)
        return 0
    if not args or args[0].startswith("-"):
        sys.exit(__doc__)
    funcs = read(args[0])
    for name in args[1:] or sorted(funcs):
        size, ops = funcs.get(name, (None, None))
        print("%-24s %5s bytes %4s ops" % (name, size, ops))
    return 0


if __name__ == "__main__":
    sys.exit(main())

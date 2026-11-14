#!/usr/bin/env python3
"""lib.map: which object in a directory defines which name, for the
linker's -L (doc/linking.md, section 5). For each NAME.syms in the
directory (OBJECT.md's symbol summary), write the line "NAME" and the
names it defines, non-local and defined, in the summary's order: the
linker adds an object for the first wanted name on its line, one pass
per level of the library's dependencies, until none remain.

The map is generated, not written by hand, so that it is always
complete: it fails if two objects define the same name, so a map can't
be ambiguous. Someone who adds objects to a library directory on an
Apple edits lib.map by hand (or regenerates it on a host).

Usage: tools/libmap.py DIR [base ...]
    writes DIR/lib.map, from DIR's NAME.syms, for every object in DIR,
    or only the named bases, as tools/measure.py's library objects are
    in its output directory beside the programs'.
"""
import os
import sys

# OBJECT.md's NAME.syms: the magic "CI" and version, five words of
# sizes, the function and data symbol counts, the modules the object
# imports, each symbol (its flags, its numbers, its name, and a PLASMA
# import's module and label), and the helpers.
SYM_DEFINED = 1
SYM_LOCAL = 8
SYM_DIMPORT = 16    # a data symbol's import flag
SYM_FIMPORT = 64    # a function's
OBJ_VERSION = 9


def die(msg):
    """Fail with the message, for a damaged summary or an ambiguous name."""
    print("libmap: " + msg, file=sys.stderr)
    sys.exit(1)


class Syms:
    """One NAME.syms file, read little-endian, OBJECT.md's format."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        if len(self.d) < 17 or self.d[0:2] != b"CI" \
                or self.d[2] != OBJ_VERSION:
            die("damaged summary: " + path)
        self.i = 3

    def word(self):
        w = self.d[self.i] | self.d[self.i + 1] << 8
        self.i += 2
        return w

    def name(self):
        n = self.d[self.i]
        self.i += 1
        s = self.d[self.i:self.i + n].decode("latin-1")
        self.i += n
        return s


def defined_names(path):
    """The names path's summary defines: non-local and defined, in the
    summary's order."""
    s = Syms(path)
    for _ in range(5):
        s.word()                     # data, strings, code, bss, zero area
    nfsyms = s.word()
    ndsyms = s.word()
    n = s.d[s.i]
    s.i += 1                         # the modules the object imports
    for _ in range(n):
        s.name()
    out = []
    for k in range(nfsyms + ndsyms):
        flags = s.d[s.i]
        s.i += 1
        data = k >= nfsyms
        if data:
            s.word()                 # offset
            s.word()                 # size
        else:
            s.i += 1                 # parameter count
            s.word()                 # offset
        s.word()                     # type hash
        nm = s.name()
        if flags & (SYM_DIMPORT if data else SYM_FIMPORT):
            s.i += 1                 # its module
            s.name()                 # its PLASMA name
        if (flags & SYM_DEFINED) and not (flags & SYM_LOCAL):
            out.append(nm)
    return out


def main():
    if len(sys.argv) < 2:
        die("usage: tools/libmap.py DIR [base ...]")
    d = sys.argv[1]
    only = set(sys.argv[2:])
    lines = []
    defines = {}
    for entry in sorted(os.listdir(d)):
        if not entry.endswith(".syms"):
            continue
        base = entry[:-len(".syms")]
        if only and base not in only:
            continue
        names = defined_names(os.path.join(d, entry))
        for nm in names:
            if nm in defines:
                die("two objects define %s: %s and %s"
                    % (nm, defines[nm], base))
            defines[nm] = base
        lines.append(base + "".join(" " + nm for nm in names))
    with open(os.path.join(d, "lib.map"), "w") as f:
        for line in lines:
            f.write(line + "\n")


main()

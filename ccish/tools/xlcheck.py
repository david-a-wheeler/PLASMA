#!/usr/bin/env python3
"""Check an XLOAD image (ccrel -x) against the REL module of the same link.

Usage: tools/xlcheck.py MODULE IMAGE

The image's format is doc/xload.md, section 3. Until XLOAD
exists, this is what tests the format: it reads both files as their
loaders would, and requires that they describe the same program:

- the same dependencies, and the same imports, by name;
- a data segment whose runs (of literal bytes and of zeros) fill it
  exactly;
- the same body (data, strings, bytecode), byte for byte, apart from
  the words either one relocates, and the bytecode at the same place;
- the same words relocated, and each to the same thing: the same
  data or string address; the same function (the REL loader sends a
  word that points into the bytecode to the function's def entry,
  and the image's word is a "def" fixup holding the function's
  number in its function table); or the same import, with the same
  addend;
- a function table of each function the REL module's RLD defines,
  and init, in address order, init last;
- fixups in address order, ended by $FF, with nothing after.

It prints the image's sizes on success, and each difference (then
exits 1) otherwise. It's written apart from ccrel, from the format's
description, so that the check doesn't share ccrel's mistakes.
"""
import sys

MODULE_BASE = 0x1000


def word(b, at):
    return b[at] | (b[at + 1] << 8)


def dci(b, at):
    """Read a DCI name at b[at]: return (name, the offset after it)."""
    name = ""
    while True:
        c = b[at]
        name += chr(c & 0x7F)
        at += 1
        if not c & 0x80:
            return name, at


def names(b, at):
    """Read DCI names to a $00: return (list, the offset after it)."""
    out = []
    while b[at]:
        n, at = dci(b, at)
        out.append(n)
    return out, at + 1


def read_rel(b):
    """Read a REL module: its dependencies, its body (from the data
    segment's start), the body's start and the bytecode's, as load
    addresses, init's address, its function entries, and its
    relocations: {body offset: ("int", word) or ("ext", name, addend)}."""
    length = word(b, 0)
    if word(b, 2) != 0x6502:
        sys.exit("xlcheck: %s: not a REL module" % sys.argv[1])
    subseg, init = word(b, 6), word(b, 10)
    deps, at = names(b, 12)
    data = MODULE_BASE + at - 2           # file offset 2 is MODULE_BASE
    end = 2 + length
    body = b[at:end]
    rld = end
    defs, relocs, raw = [], {}, []
    while b[rld]:
        t, w, x = b[rld], word(b, rld + 1), b[rld + 3]
        if t == 0x02:
            defs.append(w)
        elif t in (0x81, 0x91):
            raw.append((t, w, x))
        else:
            sys.exit("xlcheck: RLD type $%02X" % t)
        rld += 4
    esd, at = rld + 1, rld + 1
    imports = []
    while b[at]:
        n, at = dci(b, at)
        if b[at] != 0x10:
            sys.exit("xlcheck: ESD flag $%02X" % b[at])
        if word(b, at + 1) != len(imports):
            sys.exit("xlcheck: ESD %s numbered %d" % (n, word(b, at + 1)))
        imports.append(n)
        at += 3
    if at + 1 != len(b):
        sys.exit("xlcheck: %d bytes after the ESD" % (len(b) - at - 1))
    for t, w, x in raw:
        off = w + MODULE_BASE - data      # the word's body offset
        v = word(body, off)
        if t == 0x91:
            relocs[off] = ("ext", imports[x], v)
        else:
            relocs[off] = ("int", v)
    return deps, imports, body, data, subseg, init, defs, relocs


def read_image(b):
    """Read an image: its dependencies, imports, bytecode offset,
    function table, body, and fixups ({body offset: "add", "def", or
    an import's name}), the fixups' bytes, and any errors."""
    errs = []
    if b[0:2] != b"XL" or b[2] != 3 or b[3] != 0:
        sys.exit("xlcheck: %s: not a version 3 XLOAD image" % sys.argv[2])
    length, code, nfuncs = word(b, 4), word(b, 6), word(b, 8)
    deps, at = names(b, 10)
    imports, at = names(b, at)
    funcs = [word(b, at + 2 * i) for i in range(nfuncs)]
    at += 2 * nfuncs
    # The data segment, as runs: $01-$7F literal bytes; $81-$FF that
    # many less $80 zeros; $80 and a word, that many zeros.
    body, dstart = bytearray(), at
    while len(body) < code:
        c = b[at]
        at += 1
        if c == 0:
            sys.exit("xlcheck: run byte $00")
        if c < 0x80:
            body += b[at:at + c]
            at += c
        else:
            if c == 0x80:
                c = word(b, at) + 0x80
                at += 2
            body += bytes(c - 0x80)
    dfile = at - dstart
    if len(body) != code:
        errs.append("the data segment's runs overrun it, to %d of %d"
                    % (len(body), code))
    body += b[at:at + length - code]
    at += length - code
    if len(body) != length:
        sys.exit("xlcheck: the body is cut short")
    fstart = at
    fixups, cur = {}, 0
    while True:
        c = b[at]
        at += 1
        if c == 0xFF:
            break
        if c >= 0x80:
            if c - 0x80 >= len(imports):
                sys.exit("xlcheck: import %d of %d" % (c - 0x80, len(imports)))
            kind, esc = imports[c - 0x80], 0xFF
            c = b[at]
            at += 1
        else:
            kind, esc = ("def", 0x3F) if c & 0x40 else ("add", 0x3F)
            c &= 0x3F
        if c == esc:
            c = word(b, at)
            at += 2
        cur += c
        if cur + 2 > length:
            errs.append("a fixup at %d, past the body" % cur)
        fixups[cur] = kind
        cur += 2
    if at != len(b):
        errs.append("%d bytes after the fixups" % (len(b) - at))
    return (deps, imports, code, funcs, bytes(body), fixups, at - fstart,
            dfile, errs)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: tools/xlcheck.py MODULE IMAGE")
    rel = open(sys.argv[1], "rb").read()
    img = open(sys.argv[2], "rb").read()
    rdeps, rimps, rbody, data, subseg, init, rdefs, relocs = read_rel(rel)
    deps, imps, code, funcs, body, fixups, nfix, dfile, errs = \
        read_image(img)
    if deps != rdeps:
        errs.append("dependencies %s, the module's %s" % (deps, rdeps))
    if sorted(imps) != sorted(rimps) or len(imps) != len(rimps):
        errs.append("imports %s, the module's %s" % (imps, rimps))
    if len(body) != len(rbody):
        errs.append("body %d bytes, the module's %d" % (len(body),
                                                         len(rbody)))
    if code != subseg - data:
        errs.append("bytecode at %d, the module's at %d" % (code,
                                                            subseg - data))
    # The function table: the REL module's functions and init, in
    # address order, init last.
    want = sorted(d - data for d in rdefs) + [init - data]
    if funcs != want:
        errs.append("the function table differs from the module's functions")
    if funcs != sorted(funcs):
        errs.append("the function table isn't in address order")
    number = {f: i for i, f in enumerate(funcs)}
    if set(fixups) != set(relocs):
        only = sorted(set(fixups) ^ set(relocs))[:5]
        errs.append("%d words fixed up, the module's %d (first differing"
                    " body offsets %s)" % (len(fixups), len(relocs), only))
    for off in sorted(set(fixups) & set(relocs)):
        r, v, k = relocs[off], word(body, off), fixups[off]
        if r[0] == "ext":
            if k != r[1] or v != r[2]:
                errs.append("body %d: %s+%d, the module's import %s+%d"
                            % (off, k, v, r[1], r[2]))
        elif r[1] >= subseg:              # a function, by its def entry
            if k != "def" or number.get(r[1] - data) != v:
                errs.append("body %d: %s %d, not function at %d"
                            % (off, k, v, r[1] - data))
        elif k != "add" or v != (r[1] - data) & 0xFFFF:
            errs.append("body %d: %s $%04X, the module's address $%04X"
                        % (off, k, v, r[1]))
    # Every other byte is the same.
    skip = set()
    for off in relocs:
        skip.update((off, off + 1))
    diff = [i for i in range(min(len(body), len(rbody)))
            if i not in skip and body[i] != rbody[i]]
    if diff:
        errs.append("%d bytes differ, the first at body %d" % (len(diff),
                                                               diff[0]))
    if errs:
        for e in errs[:20]:
            print("xlcheck: %s: %s" % (sys.argv[2], e))
        sys.exit(1)
    print("%s: body %d (data segment %d, in %d bytes of runs), %d"
          " functions, fixups %d bytes for %d words, file %d; module %d"
          % (sys.argv[2], len(body), code, dfile, len(funcs), nfix,
             len(fixups), len(img), len(rel)))


if __name__ == "__main__":
    main()

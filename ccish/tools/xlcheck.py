#!/usr/bin/env python3
"""Check an XLOAD image (ccrel -x) against the REL module of the same link.

Usage: tools/xlcheck.py MODULE IMAGE

The image's format is doc/streaming-options.md, 5.3. Until XLOAD
exists, this is what tests the format: it reads both files as their
loaders would, and requires that they describe the same program:

- the same dependencies, and the same imports, by name;
- the same body (data, strings, bytecode), byte for byte, apart from
  the words either one relocates;
- the same words relocated, and each to the same thing: the same
  data or string address; the same function (the REL loader sends a
  word that points into the bytecode to the function's def entry,
  and the image points it at its own def entry for that function);
  or the same import, with the same addend;
- a def table with an entry for each function the REL module's RLD
  defines, and init, in address order, init last, each `JSR $03D6`;
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
    """Read an image: its dependencies, imports, def table (each
    entry's address word), body, and fixups ({image offset: None for
    the load address, or an import's name})."""
    errs = []
    if b[0:2] != b"XL" or b[2] != 1 or b[3] != 0:
        sys.exit("xlcheck: %s: not a version 1 XLOAD image" % sys.argv[2])
    length, ndefs = word(b, 4), word(b, 6)
    deps, at = names(b, 8)
    imports, at = names(b, at)
    image = b[at:at + length]
    if len(image) != length:
        sys.exit("xlcheck: the image is cut short")
    defs = []
    for i in range(ndefs):
        e = image[5 * i:5 * i + 5]
        if e[0:3] != b"\x20\xD6\x03":
            errs.append("def entry %d isn't JSR $03D6" % i)
        defs.append(word(e, 3))
    at += length
    fstart = at
    fixups, cur = {}, 0
    while True:
        c = b[at]
        at += 1
        if c == 0xFF:
            break
        imp = None
        if c >= 0x80:
            if c - 0x80 >= len(imports):
                sys.exit("xlcheck: import %d of %d" % (c - 0x80, len(imports)))
            imp = imports[c - 0x80]
            c = b[at]
            at += 1
        elif c > 0x7F:
            sys.exit("xlcheck: fixup byte $%02X" % c)
        if c == 0:
            c = word(b, at)
            at += 2
            if c == 0:
                errs.append("a fixup moves by 0")
        cur += c
        if cur + 2 > length:
            errs.append("a fixup at %d, past the image" % cur)
        fixups[cur] = imp
    if at != len(b):
        errs.append("%d bytes after the fixups" % (len(b) - at))
    return deps, imports, image, defs, fixups, at - fstart, errs


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: tools/xlcheck.py MODULE IMAGE")
    rel = open(sys.argv[1], "rb").read()
    img = open(sys.argv[2], "rb").read()
    rdeps, rimps, rbody, data, subseg, init, rdefs, relocs = read_rel(rel)
    deps, imps, image, defs, fixups, nfix, errs = read_image(img)
    if deps != rdeps:
        errs.append("dependencies %s, the module's %s" % (deps, rdeps))
    if sorted(imps) != sorted(rimps) or len(imps) != len(rimps):
        errs.append("imports %s, the module's %s" % (imps, rimps))
    base = 5 * len(defs)                  # the body's start in the image
    body = image[base:]
    if len(body) != len(rbody):
        errs.append("body %d bytes, the module's %d" % (len(body),
                                                         len(rbody)))
    code = subseg - data                  # the bytecode's body offset
    # The def table: the REL module's functions and init, in address
    # order, init last, each word fixed up.
    want = sorted(d - data for d in rdefs) + [init - data]
    got = [d - base for d in defs]
    if got != want:
        errs.append("the def table's functions differ from the module's")
    if got != sorted(got):
        errs.append("the def table isn't in address order")
    for i in range(len(defs)):
        if fixups.get(5 * i + 3, "none") is not None:
            errs.append("def entry %d's word isn't fixed up" % i)
    entry = {f: 5 * i for i, f in enumerate(got)}   # body offset -> entry
    # The body's relocated words.
    bfix = {o - base: v for o, v in fixups.items() if o >= base}
    if set(bfix) != set(relocs):
        only = sorted(set(bfix) ^ set(relocs))[:5]
        errs.append("%d words fixed up, the module's %d (first differing"
                    " body offsets %s)" % (len(bfix), len(relocs), only))
    if [o for o in fixups if o < base and (o - 3) % 5]:
        errs.append("a fixup inside the def table, not on an entry's word")
    for off in sorted(set(bfix) & set(relocs)):
        r, v = relocs[off], word(body, off)
        if r[0] == "ext":
            if bfix[off] != r[1] or v != r[2]:
                errs.append("body %d: import %s+%d, the module's %s+%d"
                            % (off, bfix[off], v, r[1], r[2]))
        elif bfix[off] is not None:
            errs.append("body %d: an import, the module's isn't" % off)
        elif r[1] >= subseg:              # a function, by its def entry
            f = r[1] - data
            if entry.get(f) != v:
                errs.append("body %d: $%04X, not function %d's entry"
                            % (off, v, f))
        elif v != (r[1] - data + base) & 0xFFFF:
            errs.append("body %d: $%04X, the module's $%04X"
                        % (off, v, r[1]))
    # Every other byte is the same.
    skip = set()
    for off in relocs:
        skip.update((off, off + 1))
    diff = [i for i in range(min(len(body), len(rbody)))
            if i not in skip and body[i] != rbody[i]]
    if diff:
        errs.append("%d bytes differ, the first at body %d" % (len(diff),
                                                               diff[0]))
    if code < 0 or code > len(body):
        errs.append("the bytecode starts outside the body")
    if errs:
        for e in errs[:20]:
            print("xlcheck: %s: %s" % (sys.argv[2], e))
        sys.exit(1)
    print("%s: image %d (defs %d, body %d), fixups %d bytes for %d words,"
          " file %d; module %d" % (sys.argv[2], len(image), base, len(body),
                                   nfix, len(fixups), len(img), len(rel)))


if __name__ == "__main__":
    main()

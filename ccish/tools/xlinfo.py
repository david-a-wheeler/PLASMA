#!/usr/bin/env python3
"""Describe an XLOAD image (ccrel -x), as XLOAD -V does.

Usage: tools/xlinfo.py IMAGE

Reads IMAGE by the format's description (doc/xload.md, section 3),
written apart from ccrel and XLOAD, so that a test can check XLOAD's
-V lines against it. Prints:

    data D, zero Z, code C, functions N, imports I
    fixups T: address A, function F, import M
    import NAME (one line each, in order)
    function OFFSET (one line each, body offsets, in order)

and checks the image as it reads it: the magic and version, the
function table in address order and inside the bytecode, the fixups in
address order inside the body and outside the zero region, each import
and function number in range, and nothing after the $FF. Exits 1, with a message, if any check fails.
"""
import sys


def fail(msg):
    sys.exit("xlinfo: %s: %s" % (sys.argv[1], msg))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/xlinfo.py IMAGE")
    b = open(sys.argv[1], "rb").read()
    word = lambda i: b[i] | (b[i + 1] << 8)
    if b[0:2] != b"XL" or b[2] != 4 or b[3] != 0:
        fail("not a version 4 XLOAD image")
    d, z, c, n = word(4), word(6), word(8), word(10)
    at = 12

    def names():
        nonlocal at
        out = []
        while b[at]:
            s = ""
            while True:
                s += chr(b[at] & 0x7F)
                at += 1
                if not b[at - 1] & 0x80:
                    break
            out.append(s)
        at += 1
        return out

    names()                       # the dependencies
    imports = names()
    funcs = [word(at + 2 * i) for i in range(n)]
    at += 2 * n
    if funcs != sorted(funcs) or len(set(funcs)) != n:
        fail("the function table isn't in address order")
    if n == 0 or funcs[0] < d + z or funcs[-1] >= d + z + c:
        fail("a function outside the bytecode")
    body = at                     # the body: data, then bytecode
    at += d + c
    if at > len(b):
        fail("cut short")

    def body_word(off):
        # The word at body offset off, from the file's data or bytecode.
        i = body + off if off < d else body + d + off - d - z
        return word(i)

    kinds = {"address": 0, "function": 0, "import": 0}
    cur = 0
    while True:
        k = b[at]
        at += 1
        if k == 0xFF:
            break
        if k >= 0x80:
            if k - 0x80 >= len(imports):
                fail("import %d of %d" % (k - 0x80, len(imports)))
            kind, g = "import", b[at]
            at += 1
            if g == 0xFF:
                g = word(at)
                at += 2
        else:
            kind = "function" if k & 0x40 else "address"
            g = k & 0x3F
            if g == 0x3F:
                g = word(at)
                at += 2
        cur += g
        if cur + 2 > d + z + c or d <= cur + 1 and cur < d + z:
            fail("a fixup at %d, outside the data and bytecode" % cur)
        if kind == "function" and body_word(cur) >= n:
            fail("function %d of %d" % (body_word(cur), n))
        kinds[kind] += 1
        cur += 2
    if at != len(b):
        fail("%d bytes after the fixups" % (len(b) - at))
    print("data %d, zero %d, code %d, functions %d, imports %d"
          % (d, z, c, n, len(imports)))
    print("fixups %d: address %d, function %d, import %d"
          % (sum(kinds.values()), kinds["address"], kinds["function"],
             kinds["import"]))
    for s in imports:
        print("import " + s)
    for f in funcs:
        print("function %d" % f)


if __name__ == "__main__":
    main()

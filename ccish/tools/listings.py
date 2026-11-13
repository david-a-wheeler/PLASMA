#!/usr/bin/env python3
"""Write a listing of each source file of ccish's programs and library.

Usage: tools/listings.py [NAME...]   (after `make`; or `make listings`)

For each source file (each program's, and the library's, from the
Makefile through measure.py), writes tools/out/lst/NAME.lst: for each
function, its comment and C source, then the machine ops ccgen writes
for it after the peephole stage (ccgen -m). The code is the small
build's (-DCCISH_SMALL, as measure.py compiles it); the full build's
passes, used here because only they have -m, write the same code,
which the tests check. NAMEs limit it to those files (parse, irin).

The ops are before linking, so calls and data are by name, and a
global the final pass moves to the heap is still an LA, not an LAW
(tools/out/PROGRAM.lst, from measure.py, has the linked code). The C
is matched to a function by name, so a function's source is found in
ccish's own style: its definition starts at the left margin and its
body ends with a "}" there. tools/opstats.py reads these listings.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure                              # noqa: E402 (its source lists)

CCOMP = measure.CCOMP
LST = os.path.join(measure.OUT, "lst")
WORK = os.path.join(LST, "work")


def sources():
    """Every source file, once, as {name: path}: the programs' (in
    src/), then the library's."""
    found = {}
    for _, names in measure.PROGRAMS:
        for n in names:
            found.setdefault(n, os.path.join(CCOMP, "src", n + ".c"))
    for n, path in sorted(measure.LIB_SOURCES.items()):
        found.setdefault(n, path)
    return found


def dump(name, path):
    """ccgen -m's listing of path, as [(function, [op lines])], in the
    order written; or None, with the message, if a pass failed."""
    base = os.path.join(WORK, name)
    for args in (["ccpp", "-DCCISH_SMALL", "-o", base, path],
                 ["cccomp", base], ["ccgen", "-m", base]):
        r = subprocess.run([os.path.join(CCOMP, args[0])] + args[1:],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, (r.stdout + r.stderr).strip()
    funcs, cur, prev_blank = [], None, True
    for line in r.stdout.splitlines():
        m = re.match(r"^    ; (\S+)$", line)
        if m and not line.startswith("    ; label") and (
                cur is None or not cur[1] or prev_blank):
            cur = (m.group(1), [])
            funcs.append(cur)
        elif line.strip() and cur is not None:
            cur[1].append(line)
        prev_blank = not line.strip()
    for suffix in (".obj", ".syms"):
        if os.path.exists(base + suffix):
            os.remove(base + suffix)
    return funcs, None


def function_source(text, name):
    """The comment and definition of function name in source text, or
    None: from the comment just above "name(" at the left margin (a
    run of // lines, or a /* */ block), to the "}" at the left margin
    that ends its body (or the end of the line, for a one-line
    function)."""
    m = re.search(r"^[A-Za-z][^\n;]*\b%s\([^;{]*?\)\s*\{" % re.escape(name),
                  text, re.M)
    if not m:
        return None
    eol = text.find("\n", m.end())
    eol = len(text) if eol < 0 else eol
    if text[m.end():eol].rstrip().endswith("}"):
        end = eol                           # a one-line function
    else:
        e = re.compile(r"\n\}(\n|$)").search(text, m.end() - 1)
        if not e:
            return None
        end = e.start() + 2
    start = m.start()
    above = text[:start].rstrip("\n")
    if above.endswith("*/"):
        c = above.rfind("/*")
        if c >= 0 and (c == 0 or text[c - 1] == "\n"):
            start = c
    else:
        while start > 0:
            prev = text.rfind("\n", 0, start - 1) + 1
            if not text.startswith("//", prev):
                break
            start = prev
    return text[start:end]


def helper_names():
    """The names of ccgen's helper functions (src/helper.c), which have
    no C source: ccgen writes their code into the objects that use
    them."""
    with open(os.path.join(CCOMP, "src", "helper.c")) as f:
        m = re.search(r"h_names\[[^]]*\]\s*=\s*\{([^}]*)\}", f.read())
    return set(re.findall(r'"(\w+)"', m.group(1))) if m else set()


def write_listing(name, path):
    """Write LST/name.lst; return (functions, ops), or None on failure."""
    funcs, err = dump(name, path)
    if funcs is None:
        print("%s: FAILED: %s" % (name, err.splitlines()[-1] if err else ""))
        return None
    with open(path) as f:
        text = f.read()
    rel = os.path.relpath(path, CCOMP)
    nops = sum(sum(1 for l in ops if not l.lstrip().startswith(";"))
               for _, ops in funcs)
    with open(os.path.join(LST, name + ".lst"), "w") as out:
        out.write("; %s: %d functions, %d ops (ccgen -m, small build)\n"
                  % (rel, len(funcs), nops))
        for fname, ops in funcs:
            n = sum(1 for l in ops if not l.lstrip().startswith(";"))
            out.write("\n;;;; %s: %d ops\n" % (fname, n))
            src = function_source(text, fname)
            if src:
                out.write(src.rstrip("\n") + "\n")
            elif fname in HELPERS:
                out.write("/* ccgen's helper function (src/helper.c) */\n")
            else:
                out.write("/* (source not found by name) */\n")
            for l in ops:
                out.write(l + "\n")
    return len(funcs), nops


HELPERS = helper_names()


def main():
    os.makedirs(WORK, exist_ok=True)
    want = set(sys.argv[1:])
    total_f = total_o = 0
    for name, path in sources().items():
        if want and name not in want:
            continue
        r = write_listing(name, path)
        if r:
            total_f += r[0]
            total_o += r[1]
    print("wrote %s: %d functions, %d ops" % (os.path.relpath(LST, CCOMP),
                                            total_f, total_o))
    return 0


if __name__ == "__main__":
    sys.exit(main())

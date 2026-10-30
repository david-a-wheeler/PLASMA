#!/usr/bin/env python3
"""Measure ccish by compiling its own source with itself.

Usage: tools/measure.py [-full]   (after `make`; writes tools/out/)

Each file in src/ is preprocessed by gcc with the stand-in headers in
tools/inc/, as the small build (-DCCISH_SMALL, approach.md decision
21; or the full build, with -full), worked around where ccish still
lacks something, and compiled with cccomp. Then each of the Apple II
programs (cccomp, ccrel) is linked with ccrel, with tools/libstub.c
standing in for the C library. The report gives each file's code, data and string sizes
(from its .syms header) and each program's totals and link workload
(from ccrel's listing). See approach.md 18.5 and 18.6 E2.

The workarounds, each something ccish doesn't accept yet:
  - unsigned, long and short become int, and number suffixes go;
  - the printf family becomes 0 (in tools/inc/stdio.h), so its calls
    and the C library itself aren't counted;
  - main(argc, argv) is renamed main_args; libstub.c's main calls it;
  - nested braces in an initializer are dropped (C's brace elision
    gives the same values);
  - parse.c's one struct ?: becomes an if.
A file that stops on one of ccish's own limits (say, "too many
variables") is compiled again by a cccomp built from a copy of the
sources with those limits raised (BIG_LIMITS), and marked so in the
report: a limit ccish itself must raise to compile that file.
"""
import os
import re
import subprocess
import sys

CCOMP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(CCOMP, "tools")
OUT = os.path.join(TOOLS, "out")
FULL = "-full" in sys.argv[1:]      # measure the full build instead

COMMON = ["diag", "bio"]
COMPILE = ["lex", "name", "type", "parse", "lower", "peep", "emit"]
# Limits raised for big_cccomp(), and the fatal messages that call
# for it.
BIG_LIMITS = {"MAX_FUNCS": 1024, "MAX_VARS": 512, "MAX_LABELS": 512,
              "MAX_TYPES": 256, "MAX_PARAM_TYPES": 2048, "MAX_TAGS": 64,
              "MAX_MEMBERS": 512, "MAX_POOL": 32000, "MAX_IDS": 1024,
              "MAX_CASES": 256}
LIMIT_MSGS = ("too many", "is full")

# The Apple II programs (B3a). ccish, both passes in one, is for the
# host only: it is too large for one module.
PROGRAMS = [
    ("cccomp", ["cccomp"] + COMMON + COMPILE),
    ("ccrel", ["ccrel"] + COMMON + ["link"]),
]


def flatten_braces(text):
    """Drop braces nested inside an initializer's outer braces."""
    out, depth, i, n = [], 0, 0, len(text)
    in_init = False
    while i < n:
        c = text[i]
        if c in "\"'":                      # copy a literal whole
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "{":
            if depth == 0:
                in_init = re.search(r"=\s*$", "".join(out[-3:])) is not None
            depth += 1
            if not (in_init and depth > 1):
                out.append(c)
        elif c == "}":
            if not (in_init and depth > 1):
                out.append(c)
            depth -= 1
        else:
            out.append(c)
        i += 1
    return "".join(out)


def prepare(name):
    """Preprocess and work around src/NAME.c; return the new file."""
    text = subprocess.run(
        ["gcc", "-E", "-P", "-nostdinc", "-I", os.path.join(TOOLS, "inc")]
        + ([] if FULL else ["-DCCISH_SMALL"])
        + [os.path.join(CCOMP, "src", name + ".c")],
        check=True, capture_output=True, text=True).stdout
    text = re.sub(r"\bunsigned (char|int)\b", r"\1", text)
    text = re.sub(r"\bunsigned\b", "int", text)
    text = re.sub(r"\b(long|short)\b", "int", text)
    text = re.sub(r"\b(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]+\b", r"\1", text)
    text = re.sub(r"\bint main\(int argc", "int main_args(int argc", text)
    text = text.replace("*o = take_b ? b : c;",
                        "if (take_b) *o = b; else *o = c;")
    text = flatten_braces(text)
    path = os.path.join(OUT, name + ".c")
    with open(path, "w") as f:
        f.write(text)
    return path


def run(args):
    """Run a command; return (succeeded, stdout + stderr)."""
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


def build_big():
    """Build OUT/big/cccomp and OUT/big/ccrel with BIG_LIMITS, from a
    copy of the sources (src/ is left alone), with the message file
    beside them; return the directory."""
    big = os.path.join(OUT, "big")
    os.makedirs(big, exist_ok=True)
    for name in os.listdir(os.path.join(CCOMP, "src")) + ["../ccish.msg"]:
        with open(os.path.join(CCOMP, "src", name)) as f:
            text = f.read()
        if name == "ccish.h":
            for limit, value in BIG_LIMITS.items():
                text = re.sub(r"(#define %s\s+)\d+" % limit,
                              r"\g<1>%d" % value, text)
        with open(os.path.join(big, os.path.basename(name)), "w") as f:
            f.write(text)
    src = lambda names: [os.path.join(big, n + ".c") for n in names]
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "cccomp")]
                   + src(["cccomp"] + COMMON + COMPILE), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccrel")]
                   + src(["ccrel"] + COMMON + ["link"]), check=True)
    return big


def module_rld(path):
    """A module's RLD and ESD bytes, and its function (def) entries."""
    with open(path, "rb") as f:
        data = f.read()
    i = 2 + (data[0] | data[1] << 8)
    rld_start, defs = i, 0
    while data[i] != 0:
        defs += data[i] == 0x02
        i += 4
    return len(data) - rld_start, defs


def summary(name):
    """The sizes in OUT/NAME.syms: data, strings, code, functions."""
    with open(os.path.join(OUT, name + ".syms"), "rb") as f:
        h = f.read(13)
    word = lambda i: h[i] | h[i + 1] << 8
    return word(3), word(5), word(7), word(9)


def main():
    os.makedirs(OUT, exist_ok=True)
    print("The %s build:" % ("full" if FULL else "small"))
    names = sorted({n for _, files in PROGRAMS for n in files})
    sizes, failed, big, over = {}, False, None, {}

    print("File        Code    Data  Strings  Functions")
    for name in names + ["libstub"]:
        src = (os.path.join(TOOLS, "libstub.c") if name == "libstub"
               else prepare(name))
        if name == "libstub":
            src = subprocess.run(
                ["gcc", "-E", "-P", "-nostdinc", "-I",
                 os.path.join(TOOLS, "inc"), src],
                check=True, capture_output=True, text=True).stdout
            with open(os.path.join(OUT, "libstub.c"), "w") as f:
                f.write(src)
            src = os.path.join(OUT, "libstub.c")
        args = ["-o", os.path.join(OUT, name), src]
        ok, msg = run([os.path.join(CCOMP, "cccomp")] + args)
        if not ok and any(m in msg for m in LIMIT_MSGS):
            over[name] = msg.strip().split(": ", 1)[-1]
            big = big or build_big()
            ok, msg = run([os.path.join(big, "cccomp")] + args)
        if not ok:
            print("%-10s  FAILED: %s" % (name, msg.strip()))
            failed = True
            continue
        sizes[name] = summary(name)
        data, strs, code, nfuncs = sizes[name]
        print("%-10s %5d   %5d    %5d      %5d%s"
              % (name, code, data, strs, nfuncs,
                 "  (over a limit: %s)" % over[name] if name in over
                 else ""))

    for prog, files in PROGRAMS:
        print()
        if any(n not in sizes for n in files):
            print("%s: not linked (a file failed)" % prog)
            continue
        objs = [os.path.join(OUT, n) for n in files + ["libstub"]]
        module = os.path.join(OUT, prog)
        # A file compiled with raised limits may be over ccrel's too.
        raised = [n for n in files if n in over]
        ccrel = os.path.join(big if raised else CCOMP, "ccrel")
        ok, msg = run([ccrel, "-l", "-o", module] + objs)
        with open(module + ".lst", "w") as f:
            f.write(msg)
        if not ok:
            print("%s: link FAILED: %s" % (prog, msg.strip().splitlines()[-1]))
            failed = True
            continue
        code = sum(sizes[n][2] for n in files)
        data = sum(sizes[n][0] for n in files)
        strs = sum(sizes[n][1] for n in files)
        rld, defs = module_rld(module)
        last = msg.strip().splitlines()[-1]
        print("%s: %d files, not counting libstub:" % (prog, len(files)))
        print("  code %d, data %d, strings %d: %d in all"
              % (code, data, strs, code + data + strs))
        print("  module file %d bytes, all read into memory to load it;"
              " %d of them RLD and ESD, freed after" % (
                  os.path.getsize(module), rld))
        print("  %d functions: %d bytes of the loader's def table, kept"
              % (defs, defs * 5))
        print("  " + last.lstrip("; "))
        if raised:
            print("  (linked by a ccrel with raised limits, for %s)"
                  % ", ".join(raised))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Profile ccish's passes, as the Apple II runs them: where their VM ops go.

Usage: tools/profile.py [-n TOP] [-f FUNCTION]... [PROGRAM...]

Run from this ccish directory, after make all and tools/measure.py
(make profile does both). PROGRAMs are ccpp, cccomp and ccgen (the
default: all three).

It builds a plvm that counts the ops it runs at each offset of each
module's bytecode: a copy of ../src/vmsrc (never changed in place),
patched with tools/plvm-profile.patch (written so it could go to
PLASMA's maintainer), in tools/out/pvm. Then it compiles each of
ccish's own sources (src/*.c, the small build, as the bootstrap does)
with measure.py's modules of ccpp, cccomp and ccgen (tools/out), each
run under that VM, and maps the counts to functions with ccrel's
listings (tools/out/PROGRAM.lst).

For each PROGRAM it prints its total ops, then its TOP functions
(default 30) by ops: their ops, share, entries (the ops run at the
function's first op, so its calls, unless a loop branches back to
it), and ops per entry. With -f, each FUNCTION's listing follows, each
op with its count: where in it the ops go.

The counts are of ops, not cycles: an LAW and an LA count the same,
though an LAW takes about 24 cycles more on the 6502.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict

CCOMP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(CCOMP, "tools", "out")
VMSRC = os.path.join(CCOMP, "..", "src", "vmsrc")
PATCH = os.path.join(CCOMP, "tools", "plvm-profile.patch")
CCSTAT = os.path.join(CCOMP, "tests", "out", "ccstat")
PROGRAMS = ["ccpp", "cccomp", "ccgen"]


def build_vm():
    """Build the profiling plvm from a patched copy of the VM's sources,
    and return its path."""
    # PLASMA's own build makes lib6502.a, which the copy links.
    subprocess.run(["make", "-s", "-C", os.path.join(CCOMP, "..", "src"),
                    "plvm"], check=True)
    pvm = os.path.join(OUT, "pvm")
    shutil.rmtree(pvm, ignore_errors=True)
    shutil.copytree(VMSRC, os.path.join(pvm, "vmsrc"))
    with open(PATCH) as f:
        subprocess.run(["patch", "-s", "-p3", "-d",
                        os.path.join(pvm, "vmsrc")], stdin=f, check=True)
    src = [os.path.join("vmsrc", n) for n in
           ("plvm.c", "cmdsys.c", "sysio.c", "a1cffa.c",
            os.path.join("lib6502", "lib6502.a"))]
    subprocess.run(["cc", "-w", "-DPLVM_PROFILE", "-o", "plvmprof"] + src,
                   cwd=pvm, check=True)
    return os.path.join(pvm, "plvmprof")


def setup(work, programs):
    """Make the directory the passes run in: the sources, the headers
    ccpp includes (sys/ccinc), the message file, and the modules."""
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "src"))
    for name in os.listdir(os.path.join(CCOMP, "src")):
        if name.endswith((".c", ".h")) and not name.startswith(","):
            shutil.copy(os.path.join(CCOMP, "src", name),
                        os.path.join(work, "src"))
    os.makedirs(os.path.join(work, "sys"))
    shutil.copytree(os.path.join(CCOMP, "lib", "include"),
                    os.path.join(work, "sys", "ccinc"))
    shutil.copy(os.path.join(CCOMP, "ccish.msg"), work)
    for p in PROGRAMS:
        mod = os.path.join(OUT, p)
        if not os.path.exists(mod):
            sys.exit("profile.py: no module %s (run tools/measure.py)" % mod)
        shutil.copy(mod, work)


def read_profile(path, counts):
    """Add the counts in the profile file at path to counts, a dict of
    module name -> {offset: count}; return the ops outside modules."""
    other, mod = 0, None
    with open(path) as f:
        for line in f:
            w = line.split()
            if w[0] == "module":
                mod = counts.setdefault(w[1], defaultdict(int))
            elif w[0] == "other":
                other += int(w[1])
            else:
                mod[int(w[0])] += int(w[1])
    return other


def run_passes(vm, work, programs):
    """Compile each source with the passes under vm; return the counts
    of each program's module, by offset."""
    counts, other = {}, 0
    env = dict(os.environ, PLVM=vm, CCSTAT=CCSTAT)
    ccrun = os.path.join(CCOMP, "tools", "ccrun")
    for src in sorted(os.listdir(os.path.join(work, "src"))):
        if not src.endswith(".c"):
            continue
        n = src[:-2]
        for p, args in (("ccpp", ["-DCCISH_SMALL", "-o", n, "src/" + src]),
                        ("cccomp", [n]), ("ccgen", [n])):
            prof = os.path.join(work, "%s.%s.prof" % (n, p))
            env["PLVM_PROFILE"] = prof
            r = subprocess.run([ccrun, "-t", "300", p] + args, cwd=work,
                               env=env, capture_output=True)
            if r.returncode != 0:
                sys.exit("profile.py: %s failed on %s:\n%s" % (
                    p, src, r.stdout.decode(errors="replace")))
            if p in programs:
                other += read_profile(prof, counts)
            os.remove(prof)
    return counts, other


def read_listing(program):
    """Read tools/out/PROGRAM.lst: return its ops, a list of (address,
    length, function, text) in address order."""
    ops, func = [], None
    with open(os.path.join(OUT, program + ".lst")) as f:
        for line in f:
            m = re.match(r"^([A-Za-z_$@][\w$@]*):$", line)
            if m:
                func = m.group(1)
                continue
            m = re.match(r"^  ([0-9A-F]{4})  ((?:[0-9A-F]{2} )+)\s*(.*)$",
                         line)
            if m and func:
                ops.append((int(m.group(1), 16), len(m.group(2)) // 3,
                            func, m.group(3).rstrip()))
    ops.sort()
    return ops


def report(program, counts, top, functions):
    """Print program's profile: its total, its top functions, and the
    annotated listings of functions."""
    ops = read_listing(program)
    base = ops[0][0]
    at = {a - base: (func, text) for a, n, func, text in ops}
    span = ops[-1][0] + ops[-1][1] - base
    mod = counts.get(program)
    if mod is None:
        sys.exit("profile.py: no counts for the module %s" % program)
    byfunc, entries, first, off_op = defaultdict(int), {}, {}, 0
    for a, n, func, text in ops:
        first.setdefault(func, a - base)
    for off, c in mod.items():
        if off not in at:
            off_op += c
            continue
        byfunc[at[off][0]] += c
    total = sum(mod.values())
    print("%s: %d ops (its bytecode %d bytes, the listing's %d)"
          % (program, total, max(mod) + 1 if mod else 0, span))
    if off_op:
        print("  %d ops at offsets that start no op in the listing:"
              " is the listing stale?" % off_op)
    print("  %10s %6s %9s %8s  function" % ("ops", "share", "entries",
                                           "ops/entry"))
    for func, c in sorted(byfunc.items(), key=lambda x: -x[1])[:top]:
        e = mod.get(first[func], 0)
        print("  %10d %5.1f%% %9d %8s  %s" % (
            c, 100.0 * c / total, e, "%.0f" % (c / e) if e else "-", func))
    for name in functions:
        lines = [(a, text) for a, n, func, text in ops if func == name]
        if not lines:
            continue
        print("\n%s, in %s: each op's count" % (name, program))
        for a, text in lines:
            print("  %10d  %04X  %s" % (mod.get(a - base, 0), a, text))
    print()


def main():
    ap = argparse.ArgumentParser(description="Profile ccish's passes.")
    ap.add_argument("-n", type=int, default=30, metavar="TOP",
                    help="functions to list for each program (30)")
    ap.add_argument("-f", action="append", default=[], metavar="FUNCTION",
                    help="also print FUNCTION's listing, with counts")
    ap.add_argument("programs", nargs="*", default=PROGRAMS,
                    metavar="PROGRAM", help="ccpp, cccomp or ccgen")
    args = ap.parse_args()
    for p in args.programs:
        if p not in PROGRAMS:
            sys.exit("profile.py: not a pass: %s" % p)
    if not os.path.exists(CCSTAT):
        sys.exit("profile.py: no ccstat module at %s (make)" % CCSTAT)
    vm = build_vm()
    work = os.path.join(OUT, "prof")
    setup(work, args.programs)
    counts, other = run_passes(vm, work, args.programs)
    print("Compiling ccish's %d sources (the small build), each pass a"
          " module under plvm." % len([s for s in os.listdir(
              os.path.join(work, "src")) if s.endswith(".c")]))
    other += sum(sum(c.values()) for m, c in counts.items()
                 if m not in PROGRAMS)
    print("Ops run outside the passes' modules (ccstat, CMDSYS): %d\n"
          % other)
    for p in args.programs:
        report(p, counts, args.n, args.f)


if __name__ == "__main__":
    main()

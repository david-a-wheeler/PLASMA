#!/usr/bin/env python3
"""Measure ccish by compiling its own source with itself.

Usage: tools/measure.py [-full]   (after `make`; writes tools/out/)

Each file in src/, and the headers it includes, is copied to
tools/out/src/ (for the full build, with the one workaround below),
then compiled with ccpp (whose default
include directory is the library's, lib/include), cccomp and ccgen, as
the small build (-DCCISH_SMALL, approach.md decision 21; or the full
build, with -full). The C library (lib/*.c) and tools/plvmexit.c's
exit are compiled too, unchanged. Then each of the Apple II programs
(ccpp, cccomp, ccgen, ccrel) is linked with ccrel, with only the
library objects it needs: each undefined reference ccrel reports adds
the object that defines it, until none is left. The report gives each
file's code, data and string sizes (from its .syms header) and each
program's totals and link workload (from ccrel's listing), and how full
ccpp's tables got (from a ccpp built with CCISH_STATS). See approach.md
18.5 and 18.6 E2.

The workaround: in the full build, the printf family becomes 0 (a
#define at the top of each file: the library has no printf, and only
the full build's listings call it). The small build compiles src/
unchanged.
A file that stops on one of ccish's own limits (say, "too many
variables") is compiled again by a cccomp and ccgen built from a copy of the
sources with those limits raised (BIG_LIMITS), and marked so in the
report: a limit ccish itself must raise to compile that file.
For parse.c, the largest source, the report also gives the sizes of
ccpp's token stream and name file beside the source text's.
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
PP = ["ppname", "pplex", "ppmac", "ppif", "ppout", "pp"]
NAM = ["nam"]                       # in both cccomp and ccgen
FRONT = ["tokin", "name", "type", "parse", "pexpr", "pstmt", "ptop",
         "irout"]
BACK = ["irin", "lower", "peep", "emit", "helper"]
# Limits raised for big_cccomp(), and the fatal messages that call
# for it.
BIG_LIMITS = {"MAX_FUNCS": 1024, "MAX_VARS": 512, "MAX_LABELS": 512,
              "MAX_TYPES": 256, "MAX_PARAM_TYPES": 2048, "MAX_TAGS": 64,
              "MAX_MEMBERS": 512, "MAX_POOL": 32000, "MAX_IDS": 1024,
              "MAX_CASES": 256, "MAX_PP_POOL": 32000, "MAX_MACROS": 1024,
              "MAX_MACRO_BYTES": 32000, "MAX_EXPAND": 64,
              "MAX_EXP_BYTES": 16000, "MAX_ARG_SLOTS": 256}

# cccomp's tables, in the order a CCISH_STATS cccomp reports them.
CC_TABLES = [("functions", "MAX_FUNCS"), ("variables visible", "MAX_VARS"),
             ("parameter types", "MAX_PARAM_TYPES"),
             ("enum constants, typedefs", "MAX_IDS"), ("types", "MAX_TYPES"),
             ("struct tags", "MAX_TAGS"), ("struct members", "MAX_MEMBERS")]

# ccpp's tables, in the order a CCISH_STATS ccpp reports them.
PP_TABLES = [("name pool bytes", "MAX_PP_POOL"), ("macros", "MAX_MACROS"),
             ("macro body bytes", "MAX_MACRO_BYTES"),
             ("expansion text bytes", "MAX_EXP_BYTES"),
             ("argument expansion bytes", "MAX_EXP_BYTES"),
             ("expansions open", "MAX_EXPAND"),
             ("argument slots", "MAX_ARG_SLOTS"),
             ("includes open", "MAX_INCLUDE"), ("groups open", "MAX_IF_NEST")]
LIMIT_MSGS = ("too many", "is full", "too long", "too deeply")

# The Apple II programs (B3a). ccish, both passes in one, is for the
# host only: it is too large for one module.
PROGRAMS = [
    ("ccpp", ["ccpp"] + COMMON + PP),
    ("cccomp", ["cccomp"] + COMMON + NAM + FRONT),
    ("ccgen", ["ccgen"] + COMMON + NAM + BACK),
    ("ccrel", ["ccrel"] + COMMON + ["link"]),
]


def prepare(path, dest):
    """Copy the file at path to dest, with the full build's workaround
    made."""
    with open(path) as f:
        text = f.read()
    if FULL and dest.endswith(".c"):
        text = ("#define printf(...) 0\n#define fprintf(...) 0\n"
                "#define sprintf(...) 0\n" + text)
    with open(dest, "w") as f:
        f.write(text)


def prepare_all():
    """Prepare every source and header (src/) into OUT/src/."""
    os.makedirs(os.path.join(OUT, "src"), exist_ok=True)
    for name in os.listdir(os.path.join(OUT, "src")):
        os.remove(os.path.join(OUT, "src", name))   # none left over
    for name in os.listdir(os.path.join(CCOMP, "src")):
        if name.endswith((".c", ".h")):
            prepare(os.path.join(CCOMP, "src", name),
                    os.path.join(OUT, "src", name))


def lib_sources():
    """The library's objects, as {name: source path}: lib/*.c, and
    tools/plvmexit.c's exit."""
    lib = os.path.join(CCOMP, "lib")
    srcs = {n[:-2]: os.path.join(lib, n) for n in os.listdir(lib)
            if n.endswith(".c")}
    srcs["plvmexit"] = os.path.join(TOOLS, "plvmexit.c")
    return srcs


def lib_object(symbol):
    """The library object that defines symbol: each defines the function
    it's named for, less a leading __ (__start, __path), but exit is
    plvmexit's."""
    return "plvmexit" if symbol == "exit" else symbol.lstrip("_")


def pp_args():
    """ccpp's options: the build."""
    return [] if FULL else ["-DCCISH_SMALL"]


def run(args):
    """Run a command; return (succeeded, stdout + stderr)."""
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


def compile_obj(bindir, name, keep=False, src=None):
    """Compile OUT/src/NAME.c (or src) into OUT/NAME.obj and .syms with
    BINDIR's ccpp, cccomp and ccgen (keeping NAME.i, NAME.nam and
    NAME.ir if keep); return (succeeded, messages)."""
    out = os.path.join(OUT, name)
    ok, msg = run([os.path.join(bindir, "ccpp"), "-o", out] + pp_args()
                  + [src or os.path.join(OUT, "src", name + ".c")])
    if ok:
        ok, msg = run([os.path.join(bindir, "cccomp")]
                      + (["-k"] if keep else []) + [out])
    if ok:
        ok, msg = run([os.path.join(bindir, "ccgen")]
                      + (["-k"] if keep else []) + [out])
    return ok, msg


def build_big():
    """Build OUT/big/cccomp, ccgen and ccrel with BIG_LIMITS, from a
    copy of the sources (src/ is left alone), with the message file
    beside them; return the directory."""
    big = os.path.join(OUT, "big")
    os.makedirs(big, exist_ok=True)
    names = [n for n in os.listdir(os.path.join(CCOMP, "src"))
             if n.endswith((".c", ".h"))]   # not an editor's files
    for name in names + ["../ccish.msg"]:
        with open(os.path.join(CCOMP, "src", name)) as f:
            text = f.read()
        for limit, value in BIG_LIMITS.items():
            text = re.sub(r"(#define %s\s+)\d+" % limit,
                          r"\g<1>%d" % value, text)
        if name == "link.c":
            # Room for every static name, so the listing names them all.
            text = re.sub(r"(#define MAX_GNAMES\s+)\d+", r"\g<1>32000", text)
            text = re.sub(r"(#define MAX_GSYMS\s+)\d+", r"\g<1>1021", text)
        with open(os.path.join(big, os.path.basename(name)), "w") as f:
            f.write(text)
    src = lambda names: [os.path.join(big, n + ".c") for n in names]
    inc = '-DCCISH_INC="%s"' % os.path.join(CCOMP, "lib", "include")
    subprocess.run(["cc", "-O2", inc, "-o", os.path.join(big, "ccpp")]
                   + src(["ccpp"] + COMMON + PP), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "cccomp")]
                   + src(["cccomp"] + COMMON + NAM + FRONT), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccgen")]
                   + src(["ccgen"] + COMMON + NAM + BACK), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccrel")]
                   + src(["ccrel"] + COMMON + ["link"]), check=True)
    subprocess.run(["cc", "-O2", "-DCCISH_STATS", inc, "-o",
                    os.path.join(big, "ccpp-stats")]
                   + src(["ccpp"] + COMMON + PP), check=True)
    subprocess.run(["cc", "-O2", "-DCCISH_STATS", "-o",
                    os.path.join(big, "cccomp-stats")]
                   + src(["cccomp"] + COMMON + NAM + FRONT), check=True)
    return big


def pp_stats(big, name):
    """How full ccpp's and cccomp's tables got for OUT/src/NAME.c
    (PP_TABLES' and CC_TABLES' order), from BIG's CCISH_STATS ccpp and
    cccomp: two lists, each None if that program failed."""
    out = os.path.join(OUT, "stats_" + name)
    ok, msg = run([os.path.join(big, "ccpp-stats"), "-o", out] + pp_args()
                  + [os.path.join(OUT, "src", name + ".c")])
    m = re.search(r"ccpp-stats((?: \d+)+)", msg)
    pp = [int(v) for v in m.group(1).split()] if ok and m else None
    cc = None
    if ok:
        ok, msg = run([os.path.join(big, "cccomp-stats"), "-k", out])
        m = re.search(r"cccomp-stats((?: \d+)+)", msg)
        cc = [int(v) for v in m.group(1).split()] if ok and m else None
    for suffix in (".i", ".nam", ".ir"):
        if os.path.exists(out + suffix):
            os.remove(out + suffix)
    return pp, cc


def limit(name):
    """The value of limit NAME, #defined in one of the sources."""
    for file in sorted(os.listdir(os.path.join(CCOMP, "src"))):
        if not file.endswith((".c", ".h")):
            continue                # not an editor's files
        with open(os.path.join(CCOMP, "src", file)) as f:
            m = re.search(r"#define %s\s+(\d+)" % name, f.read())
        if m:
            return int(m.group(1))
    raise KeyError(name)


FRAME_OPS = {"LLB", "LLW", "SLB", "SLW", "DLB", "DLW", "LLA", "ADDLB",
             "ADDLW", "IDXLB", "IDXLW"}


def parse_listing(text):
    """The functions in a ccrel -l listing: name -> dict with frame
    (bytes, 0 if no ENTER), params, calls (set of names), indirect
    (ICAL count), frame_ops (accesses to the frame) and leaves."""
    funcs, cur = {}, None
    for line in text.splitlines():
        if line.startswith("object ") or line.startswith(";"):
            continue
        m = re.match(r"^(\S[^:]*):$", line)
        if m:
            cur = funcs.setdefault(m.group(1), {
                "frame": 0, "params": 0, "calls": set(), "indirect": 0,
                "frame_ops": 0, "leaves": 0, "first": True})
            continue
        m = re.match(r"^  [0-9A-F]{4}  (?:[0-9A-F]{2} )+\s*(\S+)\s*(.*)",
                     line)
        if not m or cur is None:
            continue
        op, rest = m.group(1), m.group(2)
        if cur["first"]:
            cur["first"] = False
            e = re.match(r"(\d+),(\d+)", rest)
            if op == "ENTER" and e:
                cur["frame"], cur["params"] = int(e.group(1)), int(e.group(2))
        if op == "CALL":
            c = re.search(r";\s*(.*)$", rest)
            if c and not c.group(1).endswith("(import)"):
                cur["calls"].add(c.group(1).strip())
        elif op == "ICAL":
            cur["indirect"] += 1
        elif op == "LEAVE":
            cur["leaves"] += 1
        if op in FRAME_OPS:
            cur["frame_ops"] += 1
    return funcs


def nest_counting():
    """The parser functions that count a nesting level (call
    enter_nest()), and MAX_NEST, read from the sources."""
    names, cur = set(), None
    with open(os.path.join(CCOMP, "src", "parse.c")) as f:
        for line in f:
            m = re.match(r"^[a-z][^;=]*?\b(\w+)\(", line)
            if m and not line.rstrip().endswith(";"):
                cur = m.group(1)
            if "enter_nest();" in line and cur != "enter_nest":
                names.add(cur)
    return names, limit("MAX_NEST")


def sccs(nodes, edges):
    """Tarjan's strongly connected components: a list of sets."""
    index, low, stack, on, out = {}, {}, [], set(), []
    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(sorted(edges.get(root, ()))))]
        index[root] = low[root] = len(index)
        stack.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            w = next((w for w in it if w in nodes), None)
            if w is not None:
                if w not in index:
                    index[w] = low[w] = len(index)
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(sorted(edges.get(w, ())))))
                elif w in on:
                    low[v] = min(low[v], index[w])
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = set()
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp.add(w)
                    if w == v:
                        break
                out.append(comp)
    return out


def longest(nodes, edges, weight):
    """Longest weighted path starting at each node of a DAG (edges kept
    within nodes): node -> total weight."""
    memo = {}

    def go(v):
        if v not in memo:
            memo[v] = 0     # a cycle here would be a bug; stops loops
            memo[v] = weight(v) + max(
                [go(w) for w in edges.get(v, ()) if w in nodes] or [0])
        return memo[v]
    for v in nodes:
        go(v)
    return memo


def stack_report(funcs):
    """Print the worst-case frame stack and 6502 stack from (init), with
    recursion bounded by MAX_NEST."""
    edges = {f: {c for c in d["calls"] if c in funcs}
             for f, d in funcs.items()}
    nest, max_nest = nest_counting()
    comps = sccs(set(funcs), edges)
    comp_of = {v: i for i, c in enumerate(comps) for v in c}
    metrics = {"frame": lambda v: funcs[v]["frame"], "calls": lambda v: 1}
    bound, unbounded, groups = {}, [], []
    for i, comp in enumerate(comps):
        cyclic = len(comp) > 1 or any(v in edges[v] for v in comp)
        if not cyclic:
            v = next(iter(comp))
            bound[i] = {k: f(v) for k, f in metrics.items()}
            continue
        # Each trip around a cycle that passes a nest-counting function
        # uses up a nesting level. A cycle in the rest of the group has
        # no such bound: it is reported, and collapsed into one node
        # that counts as one trip, so the figures are lower bounds there.
        rest = comp - nest
        rcomps = sccs(rest, edges)
        loops = [c for c in rcomps
                 if len(c) > 1 or any(v in edges[v] for v in c)]
        unbounded += loops
        rof = {v: j for j, c in enumerate(rcomps) for v in c}
        redges = {}
        for v in rest:
            for w in edges[v]:
                if w in rest and rof[w] != rof[v]:
                    redges.setdefault(rof[v], set()).add(rof[w])
        b, trip = {}, {}
        for k, f in metrics.items():
            lp = longest(set(range(len(rcomps))), redges,
                         lambda j, f=f: sum(f(v) for v in rcomps[j]))
            trip[k] = max([f(n) + max([lp[rof[s]] for s in edges[n]
                                       if s in rest] or [0])
                           for n in comp & nest] or [0])
            b[k] = max_nest * trip[k] + max(lp.values() or [0])
        bound[i] = b
        groups.append((comp, trip, bool(loops)))

    cedges = {}
    for v, ws in edges.items():
        for w in ws:
            if comp_of[w] != comp_of[v]:
                cedges.setdefault(comp_of[v], set()).add(comp_of[w])
    root = "(init)" if "(init)" in funcs else "main"
    total = {}
    for k in metrics:
        t = longest(set(range(len(comps))), cedges,
                    lambda c, k=k: bound[c][k])
        total[k] = t[comp_of[root]]
    print("  stack, worst case from %s (recursion that counts nesting"
          " bounded by MAX_NEST, %d trips):" % (root, max_nest))
    print("    frame stack %s%d bytes; 6502 stack %s%d calls x 4 = %d"
          " bytes" % ("at least " if unbounded else "", total["frame"],
                      "at least " if unbounded else "", total["calls"],
                      4 * total["calls"]))
    for comp, trip, has_loops in groups:
        if not comp & nest:
            continue
        names = sorted(comp & nest)
        print("    recursion through %s: one trip <= %d frame bytes, %d"
              " calls%s" % (", ".join(names), trip["frame"], trip["calls"],
                           " (more with the unbounded recursion below)"
                           if has_loops else ""))
    for loop in unbounded:
        print("    unbounded recursion (no nest count; counted once"
              " above): %s" % ", ".join(sorted(loop)))
    big = sorted(funcs, key=lambda v: -funcs[v]["frame"])[:5]
    print("    largest frames: " + ", ".join(
        "%s %d" % (v, funcs[v]["frame"]) for v in big))
    return comps, edges


def static_report(funcs, comps, edges):
    """Print what static locals would save: for leaf functions, and for
    a compiled stack (every function that can't recurse), with today's
    opcodes (LAW/SAW: 1 byte more an access) and with new 2-byte ones."""
    recursive = {v for c in comps
                 if len(c) > 1 or any(v in edges[v] for v in c) for v in c}
    framed = {v for v, d in funcs.items() if d["frame"] > 0}

    def saving(vs, new_ops):
        # ENTER (3 bytes) goes; each LEAVE (2) becomes RET (1). Today's
        # opcodes: each frame access 1 byte more, each parameter a SAW
        # (3); new ones: one 2-byte op stores the parameters.
        total = 0
        for v in vs:
            d = funcs[v]
            s = 3 + d["leaves"]
            if new_ops:
                s -= 2 if d["params"] else 0
            else:
                s -= d["frame_ops"] + 3 * d["params"]
            total += s
        return total

    leaf = {v for v in framed - recursive
            if not funcs[v]["calls"] and not funcs[v]["indirect"]}
    nonrec = {v for v in framed - recursive if not funcs[v]["indirect"]}
    # A compiled stack overlays frames: its size is the heaviest chain of
    # frames among functions that can't recurse.
    region = longest(nonrec, edges, lambda v: funcs[v]["frame"])
    print("  static locals (bytes saved; per call, about 45 cycles saved"
          " with new opcodes):")
    for label, vs in (("leaf functions", leaf),
                      ("compiled stack (all non-recursive)", nonrec)):
        print("    %s: %d of %d framed functions, %d frame accesses;"
              " saves %d bytes with new opcodes, %d with LAW/SAW"
              % (label, len(vs), len(framed),
                 sum(funcs[v]["frame_ops"] for v in vs),
                 saving(vs, True), saving(vs, False)))
    print("    leaf region %d bytes (largest leaf frame); compiled stack"
          " region %d bytes" % (max([funcs[v]["frame"] for v in leaf]
                                    or [0]),
                                max(region.values() or [0])))


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
    tok_sizes = None
    peaks, peak_of = [0] * len(PP_TABLES), [""] * len(PP_TABLES)
    cpeaks, cpeak_of = [0] * len(CC_TABLES), [""] * len(CC_TABLES)
    prepare_all()
    big = build_big()

    libsrc = lib_sources()
    print("File        Code    Data  Strings  Functions")
    for name in names + sorted(libsrc):
        if name in libsrc:
            if name == sorted(libsrc)[0]:
                print("The library:")
            ok, msg = compile_obj(CCOMP, name, src=libsrc[name])
            if not ok:
                print("%-10s  FAILED: %s" % (name, msg.strip()))
                failed = True
                continue
            sizes[name] = summary(name)
            data, strs, code, nfuncs = sizes[name]
            print("%-10s %5d   %5d    %5d      %5d"
                  % (name, code, data, strs, nfuncs))
            continue
        stats, cstats = pp_stats(big, name)
        for i, v in enumerate(stats or []):
            if v > peaks[i]:
                peaks[i], peak_of[i] = v, name
        for i, v in enumerate(cstats or []):
            if v > cpeaks[i]:
                cpeaks[i], cpeak_of[i] = v, name
        keep = name == "parse"
        ok, msg = compile_obj(CCOMP, name, keep)
        if not ok and any(m in msg for m in LIMIT_MSGS):
            over[name] = msg.strip().split(": ", 1)[-1]
            ok, msg = compile_obj(big, name, keep)
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
        if keep:
            base = os.path.join(OUT, name)
            tok_sizes = (os.path.getsize(os.path.join(CCOMP, "src",
                                                      name + ".c")),
                         os.path.getsize(base + ".i"),
                         os.path.getsize(base + ".nam"))
            for suffix in (".i", ".nam", ".ir"):
                os.remove(base + suffix)

    if tok_sizes:
        print("\nparse.c: %d bytes of source; ccpp's token stream %d,"
              " name file %d" % tok_sizes)
    print("\nccpp's tables: the most one source needed, and the limit:")
    for (what, lim), v, who in zip(PP_TABLES, peaks, peak_of):
        print("  %-26s %6d of %6d  (%s)" % (what, v, limit(lim), who))
    print("\ncccomp's tables: the most one source needed, and the limit:")
    for (what, lim), v, who in zip(CC_TABLES, cpeaks, cpeak_of):
        print("  %-26s %6d of %6d  (%s)" % (what, v, limit(lim), who))
    for prog, files in PROGRAMS:
        print()
        if any(n not in sizes for n in files):
            print("%s: not linked (a file failed)" % prog)
            continue
        module = os.path.join(OUT, prog)
        # A file compiled with raised limits may be over ccrel's too.
        raised = [n for n in files if n in over]
        ccrel = os.path.join(big if raised else CCOMP, "ccrel")
        # Add the library object for each undefined reference (ccrel
        # stops at the first), until none is left.
        libs = []
        while True:
            objs = [os.path.join(OUT, n) for n in files + libs]
            ok, msg = run([ccrel, "-l", "-o", module] + objs)
            m = re.search(r"undefined reference to '(\w+)'", msg)
            if ok or not m or lib_object(m.group(1)) in libs \
               or lib_object(m.group(1)) not in sizes:
                break
            libs.append(lib_object(m.group(1)))
        if not ok and any(m in msg for m in LIMIT_MSGS):
            # Over one of ccrel's own limits: link with raised limits,
            # and say which.
            raised.append("ccrel: " + msg.strip().splitlines()[-1]
                          .split(": ", 1)[-1])
            big = big or build_big()
            ccrel = os.path.join(big, "ccrel")
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
        lcode = sum(sizes[n][2] for n in libs)
        ldata = sum(sizes[n][0] for n in libs)
        lstrs = sum(sizes[n][1] for n in libs)
        rld, defs = module_rld(module)
        last = msg.strip().splitlines()[-1]
        print("%s: %d files, and %d library objects:"
              % (prog, len(files), len(libs)))
        print("  its own code %d, data %d, strings %d: %d in all"
              % (code, data, strs, code + data + strs))
        print("  the library's code %d, data %d, strings %d: %d in all"
              " (%s)" % (lcode, ldata, lstrs, lcode + ldata + lstrs,
                         " ".join(sorted(libs))))
        print("  module file %d bytes, all read into memory to load it;"
              " %d of them RLD and ESD, freed after" % (
                  os.path.getsize(module), rld))
        print("  %d functions: %d bytes of the loader's def table, kept"
              % (defs, defs * 5))
        print("  " + last.lstrip("; "))
        if raised:
            print("  (linked by a ccrel with raised limits, for %s)"
                  % ", ".join(raised))
        # Link again with a ccrel that has room for every static name,
        # for the call graph.
        big = big or build_big()
        named = os.path.join(OUT, prog + "-named")
        ok, msg = run([os.path.join(big, "ccrel"), "-l", "-o", named]
                      + objs)
        if os.path.exists(named):
            os.remove(named)
        if ok:
            funcs = parse_listing(msg)
            comps, edges = stack_report(funcs)
            static_report(funcs, comps, edges)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

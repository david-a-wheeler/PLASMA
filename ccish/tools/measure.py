#!/usr/bin/env python3
"""Measure ccish by compiling its own source with itself.

Usage: tools/measure.py [-full]   (after `make`; writes tools/out/)

Each file in src/, and the headers it includes, is copied to
tools/out/src/ (for the full build, with the one workaround below),
then compiled with ccpp (whose default
include directory is the library's, lib/include), cccomp and ccgen, as
the small build (-DCCISH_SMALL, approach.md decision 21; or the full
build, with -full). The C library (lib/*.c) is
compiled too, unchanged (the programs' sources and the
library's are the Makefile's, from make print-sources). Then each of
the Apple II programs
(ccpp, cccomp, ccgen, ccrel) is linked with ccrel's -L, which adds
the library objects it needs; the program's objects, then the
library's, are written to OUT/PROGRAM.objs, from the listing. The report gives each
file's code, data and string sizes (from its .syms header) and each
program's totals and link workload (from ccrel's listing), and how full
ccpp's tables got (from a ccpp built with CCISH_STATS), and what
each program needs on a 64K Apple II, running and loading, in the
form that runs there (APPLE_LINK: XLOAD images, loaded through XLOAD;
tests/special/fit.sh checks that each fits).
See approach.md 18.5 and 18.6 E2. It ends with each measured limit's peak, the most
ccish's own source uses, which tools/limits.sh puts in doc/limits.md.
`make measure` writes the report to doc/measure.txt (checked in), and
`make test` keeps it current.

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
LIBMAP = os.path.join(TOOLS, "libmap.py")
OUT = os.path.join(TOOLS, "out")
FULL = "-full" in sys.argv[1:]      # measure the full build instead

# For each program's fit on a 64K Apple II. The free memory and
# FILEIO's size change only with PLASMA or the machine, so they're
# measured once, not here.
APPLE_FREE = 41171      # free at PLASMA 2.20's prompt, before FILEIO
                        # or any module: MAME's 64K enhanced IIe,
                        # 2026-11-09 (approach.md, "Free memory,
                        # measured")
FILEIO_RESIDENT = 1998  # what the loader keeps of FILEIO (as for
                        # ccish's, loader_sizes()): its body without
                        # the header, and its def table; from assembling
                        # src/libsrc/apple/fileio.pla, 2026-11-11
IO_BUFFER = 1280        # ProDOS's 1K buffer for each open file but the
                        # first (which uses the system one at $0800),
                        # at its worst: FILEIO's iobufalloc takes each
                        # from the heap page-aligned (heapallocalign(...,
                        # 8, ...)), which skips 1 to 256 bytes, and
                        # ccish's fopen asks for them one at a time
# The form that runs on an Apple II: ccrel's flags for it. "-x" for
# XLOAD images, as tools/a2disk builds them (doc/xload.md); [] for REL
# modules. The fit lines and the summary follow the form of the file
# this links (an image starts with "XL"), so they describe what runs.
APPLE_LINK = ["-x"]
XLOAD = os.path.join(CCOMP, "tests", "out", "xload", "xload")
XLOAD2 = XLOAD + "2"            # XLOAD's second part, an image beside it
IMAGE_HEADER = 12               # an image's fixed header (doc/xload.md, 3.2)
FRAME_SPARE = 512               # what XLOAD leaves for frames, as
                                # HEAP_SPARE does (xload.c, xload2.c)
# The files each program holds open at once (from its bopen() calls),
# and what they are; for ccpp, beside its #include files, whose most
# open at once is measured ("includes open"), the {} in its text.
OPEN_FILES = {
    "ccpp": (2, "the source, {} includes at most, and NAME.i"),
    "cccomp": (2, "NAME.i and NAME.ir"),
    "ccgen": (3, "NAME.ir, NAME.nam and the object"),
    "ccrel": (2, "an object and the module"),
}



def make_sources():
    """The Apple II programs (B3a), each with its sources' names in link
    order, and the library's sources as {name: path}: from the Makefile
    (make print-sources), which keeps them for the builds and the
    bootstrap too. (ccish, all the passes in one, is for the host only:
    it is too large for one module.)"""
    out = subprocess.run(["make", "-s", "--no-print-directory",
                          "print-sources"], cwd=CCOMP, check=True,
                         capture_output=True, text=True).stdout
    progs, lib = [], {}
    for line in out.splitlines():
        name, *paths = line.split()
        names = [os.path.basename(p)[:-2] for p in paths]
        if name == "lib":
            lib = {n: os.path.join(CCOMP, p) for n, p in zip(names, paths)}
        else:
            progs.append((name, names))
    return progs, lib


PROGRAMS, LIB_SOURCES = make_sources()
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
             ("struct tags", "MAX_TAGS"), ("struct members", "MAX_MEMBERS"),
             ("ordinary labels", "ENTRY_LABEL"), ("nesting", "MAX_NEST"),
             ("case labels open", "MAX_CASES")]

# ccgen's per-function tables, in the order a CCISH_STATS ccgen reports
# them: the most any one function used.
GEN_TABLES = [("code bytes", "MAX_FUNC_CODE"),
              ("relocations", "MAX_FUNC_RELOCS"),
              ("frame-size bytes", "MAX_RETURNS"),
              ("temporaries' operands", "MAX_TEMP_USES")]

# ccpp's tables, in the order a CCISH_STATS ccpp reports them.
PP_TABLES = [("name pool bytes", "MAX_PP_POOL"), ("macros", "MAX_MACROS"),
             ("macro body bytes", "MAX_MACRO_BYTES"),
             ("expansion text bytes", "MAX_EXP_BYTES"),
             ("argument expansion bytes", "MAX_EXP_BYTES"),
             ("expansions open", "MAX_EXPAND"),
             ("argument slots", "MAX_ARG_SLOTS"),
             ("includes open", "MAX_INCLUDE"), ("groups open", "MAX_IF_NEST")]
LIMIT_MSGS = ("too many", "is full", "too long", "too deeply")



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
    src = lambda prog: [os.path.join(big, n + ".c")
                        for n in dict(PROGRAMS)[prog]]
    inc = '-DCCISH_INC="%s"' % os.path.join(CCOMP, "lib", "include")
    subprocess.run(["cc", "-O2", inc, "-o", os.path.join(big, "ccpp")]
                   + src("ccpp"), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "cccomp")]
                   + src("cccomp"), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccgen")]
                   + src("ccgen"), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccrel")]
                   + src("ccrel"), check=True)
    subprocess.run(["cc", "-O2", "-DCCISH_STATS", inc, "-o",
                    os.path.join(big, "ccpp-stats")]
                   + src("ccpp"), check=True)
    subprocess.run(["cc", "-O2", "-DCCISH_STATS", "-o",
                    os.path.join(big, "cccomp-stats")]
                   + src("cccomp"), check=True)
    subprocess.run(["cc", "-O2", "-DCCISH_STATS", "-o",
                    os.path.join(big, "ccgen-stats")]
                   + src("ccgen"), check=True)
    return big


def pp_stats(big, name):
    """How full ccpp's, cccomp's and ccgen's tables got for
    OUT/src/NAME.c (PP_TABLES', CC_TABLES' and GEN_TABLES' order), from
    BIG's CCISH_STATS ccpp, cccomp and ccgen: three lists, each None if
    that program failed."""
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
    gen = None
    if ok:
        ok, msg = run([os.path.join(big, "ccgen-stats"), out])
        m = re.search(r"ccgen-stats((?: \d+)+)", msg)
        gen = [int(v) for v in m.group(1).split()] if ok and m else None
    for suffix in (".i", ".nam", ".ir", ".obj", ".syms"):
        if os.path.exists(out + suffix):
            os.remove(out + suffix)
    return pp, cc, gen


def longest_line(paths):
    """The longest line in the files at paths, after joining the lines
    that end in a backslash, as ccpp's lexer does: (length, file)."""
    best = (0, "")
    for path in paths:
        with open(path) as f:
            text = f.read().replace("\\\n", "")
        for line in text.split("\n"):
            if len(line) > best[0]:
                best = (len(line), os.path.basename(path))
    return best


def limit(name):
    """The value of limit NAME, #defined in one of the sources, in the
    build measured: one with a value for each build has the small
    build's first and the full build's last (CCISH_BIG_LIMITS,
    build.h)."""
    for file in sorted(os.listdir(os.path.join(CCOMP, "src"))):
        if not file.endswith((".c", ".h")):
            continue                # not an editor's files
        with open(os.path.join(CCOMP, "src", file)) as f:
            values = re.findall(r"#define %s\s+(\d+)" % name, f.read())
        if values:
            return int(values[-1 if FULL else 0])
    raise KeyError(name)


FRAME_OPS = {"LLB", "LLW", "SLB", "SLW", "DLB", "DLW", "LLA", "ADDLB",
             "ADDLW", "IDXLB", "IDXLW"}


def parse_listing(text):
    """The functions in a ccrel -v listing: name -> dict with frame
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
    # Sets order by string hashes, which differ from run to run: sort,
    # so that the report doesn't.
    for comp, trip, has_loops in sorted(groups,
                                        key=lambda g: sorted(g[0])):
        if not comp & nest:
            continue
        names = sorted(comp & nest)
        print("    recursion through %s: one trip <= %d frame bytes, %d"
              " calls%s" % (", ".join(names), trip["frame"], trip["calls"],
                           " (more with the unbounded recursion below)"
                           if has_loops else ""))
    for loop in sorted(unbounded, key=sorted):
        print("    unbounded recursion (no nest count; counted once"
              " above): %s" % ", ".join(sorted(loop)))
    big = sorted(funcs, key=lambda v: -funcs[v]["frame"])[:5]
    print("    largest frames: " + ", ".join(
        "%s %d" % (v, funcs[v]["frame"]) for v in big))
    return comps, edges, total["frame"], bool(unbounded)


def loader_sizes(path):
    """What PLASMA's Apple II loader (loadmod, src/vmsrc/apple/cmd.pla)
    allocates for the module at path: its body, without the length
    word, header and dependency list, which it doesn't keep; and its
    def table, 5 bytes for each function the header counts (init too)
    and an end byte."""
    with open(path, "rb") as f:
        data = f.read()
    i = 12                          # the dependencies, each a DCI name
    while data[i] != 0:
        while data[i] & 0x80:
            i += 1
        i += 1
    kept = (data[0] | data[1] << 8) - (i + 1 - 2)
    return kept, 5 * (data[8] | data[9] << 8) + 1


def fit_report(prog, module, rld, heap, frame, unbounded, includes):
    """Print what prog needs on a 64K Apple II, running and loading, with
    the RLD streamed; tests/special/fit.sh checks the "needs" line. rld is
    the module's RLD and ESD bytes."""
    files, what = OPEN_FILES[prog]
    if prog == "ccpp":
        files += includes
    what = what.format(includes)
    body, deftbl = loader_sizes(module)
    bufs = (files - 1) * IO_BUFFER
    need = body + deftbl + heap + frame + FILEIO_RESIDENT + bufs
    print("  on a 64K Apple II (%d bytes free), the RLD streamed:"
          % APPLE_FREE)
    print("    module body %d, def table %d, zero-filled globals on the"
          " heap %d," % (body, deftbl, heap))
    print("    frame stack %s%d, FILEIO %d," % (
        "at least " if unbounded else "", frame, FILEIO_RESIDENT))
    print("    I/O buffers %d x %d at most: %d files open (%s)" % (
        files - 1, IO_BUFFER, files, what))
    print("    running, needs %d of %d: %d %s" % (
        need, APPLE_FREE, abs(APPLE_FREE - need),
        "to spare" if need <= APPLE_FREE else "SHORT"))
    if unbounded:
        print("    (a lower bound: the frame stack takes more with the"
              " unbounded recursion above)")
    load = FILEIO_RESIDENT + body + deftbl
    # Today's loader reads the RLD and ESD in after the body, past the
    # heap it allocated, and frees them after.
    now = load + rld
    print("    loading, the most at once: %d with the RLD streamed, %d"
          " today (the RLD and ESD too)" % (load, now))
    return {"heap": heap, "module": os.path.getsize(module), "load": load,
            "now": now, "need": need, "unbounded": unbounded}


def is_image(path):
    """Is the file at path an XLOAD image (ccrel -x), not a REL module?"""
    with open(path, "rb") as f:
        return f.read(2) == b"XL"


def image_sizes(path):
    """An XLOAD image's sizes (doc/xload.md, 3.2): data, zero region,
    code, functions (init too), fixup bytes (with the end byte), and the
    file; and what it takes in memory, placed: its def entries, 5 bytes
    each on the 64K VM, then data, zero region and code."""
    with open(path, "rb") as f:
        b = f.read()
    word = lambda i: b[i] | b[i + 1] << 8
    d, z, c, n = word(4), word(6), word(8), word(10)
    at = IMAGE_HEADER
    for _ in range(2):              # the dependencies, then the imports
        while b[at]:
            while b[at] & 0x80:
                at += 1
            at += 1
        at += 1
    names = at - IMAGE_HEADER
    at += 2 * n + d + c             # the function table, data, bytecode
    return {"d": d, "z": z, "c": c, "n": n, "fixups": len(b) - at,
            "names": names, "file": len(b), "mem": 5 * n + d + z + c}


def xload_sizes():
    """XLOAD's file, what its first part takes in memory once loaded
    (its body and def table, at the heap's bottom, and the zero-filled
    globals its init puts on the heap, which a listing of it gives), and
    its second part's placed size; None if XLOAD isn't built (make
    xload)."""
    if not (os.path.exists(XLOAD) and os.path.exists(XLOAD2)):
        return None
    body, deftbl = loader_sizes(XLOAD)
    tmp = os.path.join(OUT, "xload-l")
    ok, msg = run([os.path.join(CCOMP, "ccish"), "-v", "-o", tmp,
                   os.path.join(CCOMP, "xload", "xload.c")])
    if os.path.exists(tmp):
        os.remove(tmp)
    if not ok:
        return None
    heap = sum(int(m.group(1)) for m in re.finditer(
        r"^; heap global \d+: \S+, (\d+) bytes$", msg, re.M))
    return {"file": os.path.getsize(XLOAD), "first": body + deftbl + heap,
            "file2": os.path.getsize(XLOAD2),
            "second": image_sizes(XLOAD2)["mem"]}


def image_fit(prog, image, frame, unbounded, includes, xl):
    """Print what prog, an XLOAD image, needs on a 64K Apple II, running
    and loading through XLOAD; tests/special/fit.sh checks the "needs"
    lines. Loading, it's XLOAD's own checks (xload.c, xload2.c): the
    program goes at the heap's bottom, where XLOAD's module starts, and
    XLOAD's second part goes in its zero region if it fits there above
    the first part, else above the program; both leave FRAME_SPARE
    for frames."""
    files, what = OPEN_FILES[prog]
    if prog == "ccpp":
        files += includes
    what = what.format(includes)
    s = image_sizes(image)
    bufs = (files - 1) * IO_BUFFER
    top = s["mem"]
    need = FILEIO_RESIDENT + top + frame + bufs
    print("  as an XLOAD image: file %d bytes; data %d, zero region %d,"
          " code %d," % (s["file"], s["d"], s["z"], s["c"]))
    print("    %d functions (init too), fixups %d bytes"
          % (s["n"], s["fixups"]))
    print("  on a 64K Apple II (%d bytes free), through XLOAD:"
          % APPLE_FREE)
    print("    def table %d, data %d, zero region %d, code %d,"
          % (5 * s["n"], s["d"], s["z"], s["c"]))
    print("    frame stack %s%d, FILEIO %d," % (
        "at least " if unbounded else "", frame, FILEIO_RESIDENT))
    print("    I/O buffers %d x %d at most: %d files open (%s)" % (
        files - 1, IO_BUFFER, files, what))
    print("    running, needs %d of %d: %d %s" % (
        need, APPLE_FREE, abs(APPLE_FREE - need),
        "to spare" if need <= APPLE_FREE else "SHORT"))
    if unbounded:
        print("    (a lower bound: the frame stack takes more with the"
              " unbounded recursion above)")
    r = {"unbounded": unbounded, "need": need, "load": None}
    r.update(s)
    if xl is None:
        print("    loading: not measured (no XLOAD: make xload)")
        return r
    zero = 5 * s["n"] + s["d"]
    t = max(zero, xl["first"])
    where = "in its zero region"
    if t + xl["second"] > zero + s["z"]:
        t, where = max(top, xl["first"]), "above it"
    load = FILEIO_RESIDENT + max(top, t + xl["second"]) + FRAME_SPARE
    r["load"] = load
    print("    loading, needs %d of %d: %d %s (XLOAD's second part %s,"
          % (load, APPLE_FREE, abs(APPLE_FREE - load),
             "to spare" if load <= APPLE_FREE else "SHORT", where))
    print("    at %d to %d above the heap's bottom, the zero region %d to"
          " %d; %d for frames)" % (t, t + xl["second"], zero,
                                   zero + s["z"], FRAME_SPARE))
    return r


def summary_table(rows):
    """Print the summary at the report's end: for each program (rows,
    in PROGRAMS' order: its sizes, or None if it wasn't measured) its
    sizes, then whether it loads, with today's loader and with one
    that streams the RLD, and runs on a 64K Apple II, and its margin."""
    yes = lambda need: "yes" if need <= APPLE_FREE else "NO"
    build = "full" if FULL else "small"
    print("\nSummary: the %s build's modules, their sizes:" % build)
    print("%-7s %5s  %6s %5s %7s %5s %5s %4s %6s" % (
        "Program", "Heap", "Code", "Data", "Strings", "Other", "RLD", "ESD",
        "Module"))
    for prog, r in rows:
        if r is None:
            print("%-7s not measured (a file or the link failed)" % prog)
            continue
        print("%-7s %5d  %6d %5d %7d %5d %5d %4d %6d" % (
            prog, r["heap"], r["code"], r["data"], r["strs"],
            r["body"] - r["code"] - r["data"] - r["strs"], r["rld"],
            r["esd"], r["module"]))
    print("Module: the file, the sum of Code through ESD: the body"
          " (code, data and\nstrings, the program's and the library's,"
          " and other, the linker's: header,\ndependencies, init, helpers"
          " and zero-filled data that stays), then the RLD\nand ESD, read"
          " only to load it. Heap: zero-filled globals, not in the file,"
          "\nput on the heap at start.")
    print("\nAnd their fit on a 64K Apple II (%d bytes free):" % APPLE_FREE)
    print("%-7s %s %s %s" % ("", "--- Load today ----",
                             "-- Load streamed --", "----- Running -----"))
    print("%-7s" % "Program" + " %6s %6s  %-3s" % ("Need", "Spare", "OK?") * 3)
    for prog, r in rows:
        if r is None:
            continue
        print("%-7s" % prog + "".join(
            " %6d %6d  %-3s" % (n, APPLE_FREE - n, yes(n) + m)
            for n, m in ((r["now"], ""), (r["load"], ""),
                         (r["need"], "*" if r["unbounded"] else ""))
        ).rstrip())
    print("Load today: the whole file read at once, as PLASMA's"
          " loaders do; streamed:\nthe body, with the RLD applied as it's"
          " read (doc/plasma-requests.md, item 1).\nEach need counts"
          " FILEIO (%d) and the loader's def table; running, the heap,\n"
          "frame stack and I/O buffers (at their worst, %d each) too, the"
          " same with\neither loader." % (FILEIO_RESIDENT, IO_BUFFER))
    if any(r and r["unbounded"] for _, r in rows):
        print("* A lower bound: unbounded recursion can take more frame"
              " stack.")


def image_summary(rows, xl):
    """Print the summary at the report's end when what runs on an Apple
    II is XLOAD images: for each program (rows, as for summary_table) its
    image's sizes, then XLOAD's, then whether each loads and runs on a
    64K Apple II, and its margin."""
    yes = lambda need: "yes" if need <= APPLE_FREE else "NO"
    build = "full" if FULL else "small"
    print("\nDisk space: %s build programs (XLOAD images) file sizes +"
          " XLOAD itself:" % build.capitalize())
    print("%-7s %5s %5s %5s %6s %6s %6s %6s %6s" % (
        "Program", "Names", "Table", "Data", "(Zero)", "Code", "Fixups",
        "(REL)", "File"))
    for prog, r in rows:
        if r is None:
            print("%-7s not measured (a file or the link failed)" % prog)
            continue
        assert r["file"] == (IMAGE_HEADER + r["names"] + 2 * r["n"]
                             + r["d"] + r["c"] + r["fixups"])
        print("%-7s %5d %5d %5d %6d %6d %6d %6d %6d" % (
            prog, r["names"], 2 * r["n"], r["d"], r["z"], r["c"],
            r["fixups"], r["module"], r["file"]))
    if xl:
        print("%-7s %52d" % ("XLOAD", xl["file"]))  # under File
        r = image_sizes(XLOAD2)
        print("%-7s %5d %5d %5d %6d %6d %6d %6s %6d" % (
            "XLOAD2", r["names"], 2 * r["n"], r["d"], r["z"], r["c"],
            r["fixups"], "", r["file"]))
    print("%-7s %52d" % ("Total", sum(r["file"] for _, r in rows if r)
                         + (xl["file"] + xl["file2"] if xl else 0)))
    print("File: the whole file's bytes; Total, all of them. An image"
          " file's parts, in\nits order: a %d-byte header (the same in"
          " every image), Names (the\ndependencies' and imports'), Table"
          " (the function table, 2 bytes a function,\ninit too), Data,"
          " Code and Fixups. Not in the file: (Zero), the zero region,\nthe"
          " disk space saved, shown where it goes in memory, after Data"
          " (init\nclears it); (REL), the same program as a REL module, for"
          " comparison. XLOAD\nis a REL module, and XLOAD2, its second"
          " part, an image." % IMAGE_HEADER)
    if xl:
        print("XLOAD in memory: its first part %d, its second part %d,"
              " placed\n(none of it stays once the program runs)."
              % (xl["first"], xl["second"]))
    print("\nAnd their fit on a 64K Apple II (%d bytes free), through"
          " XLOAD:" % APPLE_FREE)
    print("%-7s %s %s" % ("", "----- Loading -----", "----- Running -----"))
    print("%-7s" % "Program" + " %6s %6s  %-3s" % ("Need", "Spare", "OK?") * 2)
    if xl:
        print("%-7s %6d %6d  %s" % ("XLOAD", xl["now"], APPLE_FREE - xl["now"],
                                    yes(xl["now"])))
    for prog, r in rows:
        if r is None:
            continue
        load = ("  %6s %6s  %-3s" % ("-", "-", "?") if r["load"] is None
                else " %6d %6d  %-3s" % (r["load"], APPLE_FREE - r["load"],
                                         yes(r["load"])))
        print("%-7s" % prog + load + (" %6d %6d  %-3s" % (
            r["need"], APPLE_FREE - r["need"],
            yes(r["need"]) + ("*" if r["unbounded"] else ""))).rstrip())
    print("XLOAD: PLASMA's loader reading XLOAD's whole file (RLD and ESD"
          " too) and FILEIO\n(%d). A program loading: FILEIO, the program"
          " (its def table, data, zero\nregion and code), XLOAD's second"
          " part if it's above the program, and %d\nfor frames, as XLOAD"
          " checks; running: FILEIO, the program, the frame stack\nand"
          " I/O buffers (at their worst, %d each)."
          % (FILEIO_RESIDENT, FRAME_SPARE, IO_BUFFER))
    if any(r and r["unbounded"] for _, r in rows):
        print("* A lower bound: unbounded recursion can take more frame"
              " stack.")


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
    """A module's RLD bytes (with its end byte), its ESD bytes, and its
    function (def) entries."""
    with open(path, "rb") as f:
        data = f.read()
    i = 2 + (data[0] | data[1] << 8)
    rld_start, defs = i, 0
    while data[i] != 0:
        defs += data[i] == 0x02
        i += 4
    return i + 1 - rld_start, len(data) - (i + 1), defs


def note_peak(peaks, lim, value, where):
    """Keep value as limit LIM's peak, from where, if it's the most."""
    if value > peaks.get(lim, (-1, ""))[0]:
        peaks[lim] = (value, where)


def summary(name):
    """The sizes in OUT/NAME.syms: data, strings, code, zero-filled
    (the zero-filled globals, which ccrel may move to the heap, and the
    zero area's static locals)."""
    with open(os.path.join(OUT, name + ".syms"), "rb") as f:
        h = f.read(13)
    word = lambda i: h[i] | h[i + 1] << 8
    return word(3), word(5), word(7), word(9) + word(11)


def main():
    os.makedirs(OUT, exist_ok=True)
    print("The %s build:" % ("full" if FULL else "small"))
    names = sorted({n for _, files in PROGRAMS for n in files})
    sizes, failed, big, over, fit = {}, False, None, {}, {}
    tok_sizes = None
    peaks, peak_of = [0] * len(PP_TABLES), [""] * len(PP_TABLES)
    cpeaks, cpeak_of = [0] * len(CC_TABLES), [""] * len(CC_TABLES)
    gpeaks, gpeak_of = [0] * len(GEN_TABLES), [""] * len(GEN_TABLES)
    prepare_all()
    big = build_big()
    xl = xload_sizes()
    if xl:
        rld, esd, _ = module_rld(XLOAD)
        xl["now"] = FILEIO_RESIDENT + xl["first"] + rld + esd

    libsrc = LIB_SOURCES
    print("File        Code    Data  Strings     Zero")
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
            data, strs, code, zero = sizes[name]
            print("%-10s %5d   %5d    %5d      %5d"
                  % (name, code, data, strs, zero))
            continue
        stats, cstats, gstats = pp_stats(big, name)
        for i, v in enumerate(stats or []):
            if v > peaks[i]:
                peaks[i], peak_of[i] = v, name
        for i, v in enumerate(cstats or []):
            if v > cpeaks[i]:
                cpeaks[i], cpeak_of[i] = v, name
        for i, v in enumerate(gstats or []):
            if v > gpeaks[i]:
                gpeaks[i], gpeak_of[i] = v, name
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
        data, strs, code, zero = sizes[name]
        print("%-10s %5d   %5d    %5d      %5d%s"
              % (name, code, data, strs, zero,
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
    limit_peaks = {}
    print("\nccpp's tables: the most one source needed, and the limit:")
    for (what, lim), v, who in zip(PP_TABLES, peaks, peak_of):
        print("  %-26s %6d of %6d  (%s)" % (what, v, limit(lim), who))
        note_peak(limit_peaks, lim, v, who + ".c")
    print("\ncccomp's tables: the most one source needed, and the limit:")
    for (what, lim), v, who in zip(CC_TABLES, cpeaks, cpeak_of):
        print("  %-26s %6d of %6d  (%s)" % (what, v, limit(lim), who))
        note_peak(limit_peaks, lim, v, who + ".c")
    print("\nccgen's tables: the most one function needed, and the limit:")
    for (what, lim), v, who in zip(GEN_TABLES, gpeaks, gpeak_of):
        print("  %-26s %6d of %6d  (%s)" % (what, v, limit(lim), who))
        note_peak(limit_peaks, lim, v, who + ".c")
    sources = [os.path.join(OUT, "src", n) for n in
               sorted(os.listdir(os.path.join(OUT, "src")))]
    sources += [os.path.join(CCOMP, "lib", "include", n) for n in
                sorted(os.listdir(os.path.join(CCOMP, "lib", "include")))]
    sources += [libsrc[n] for n in sorted(libsrc)]
    v, who = longest_line(p for p in sources if p.endswith((".c", ".h")))
    print("\nThe longest source line: %d of %d characters  (%s)"
          % (v, limit("MAX_LINE"), who))
    note_peak(limit_peaks, "MAX_LINE", v, who)
    # The library's map, for the link's -L (its objects are in OUT,
    # beside the programs').
    ok, msg = run([sys.executable, LIBMAP, OUT] + sorted(LIB_SOURCES))
    if not ok:
        print("lib.map FAILED: %s" % msg.strip())
        failed = True
    for prog, files in PROGRAMS:
        print()
        if any(n not in sizes for n in files):
            print("%s: not linked (a file failed)" % prog)
            continue
        module = os.path.join(OUT, prog)
        # A file compiled with raised limits may be over ccrel's too.
        raised = [n for n in files if n in over]
        ccrel = os.path.join(big if raised else CCOMP, "ccrel")
        # ccrel's -L adds the library objects the program needs, from
        # OUT's lib.map; the listing names them.
        def link(ccrel):
            return run([ccrel, "-v", "-L", OUT, "-o", module]
                       + [os.path.join(OUT, n) for n in files])
        ok, msg = link(ccrel)
        if not ok and any(m in msg for m in LIMIT_MSGS):
            # Over one of ccrel's own limits: link with raised limits,
            # and say which.
            raised.append("ccrel: " + msg.strip().splitlines()[-1]
                          .split(": ", 1)[-1])
            big = big or build_big()
            ccrel = os.path.join(big, "ccrel")
            ok, msg = link(ccrel)
        with open(module + ".lst", "w") as f:
            f.write(msg)
        if not ok:
            print("%s: link FAILED: %s" % (prog, msg.strip().splitlines()[-1]))
            failed = True
            continue
        # The objects linked: the program's, then the ones -L added,
        # named by the listing; the list, for tools/a2disk and
        # xload_self.sh, and the call graph's relink below.
        libs = [m.group(1) for m in re.finditer(
            r"^; library object: (\S+)$", msg, re.M)]
        objs = [os.path.join(OUT, n) for n in files + libs]
        with open(module + ".objs", "w") as f:
            f.write("".join(o + "\n" for o in objs))
        # The bases the linker's -L adds, each with its NUL, in
        # link.c's LIB_NAMES.
        note_peak(limit_peaks, "LIB_NAMES",
                  sum(len(o) + 1 for o in libs), "linking " + prog)
        code = sum(sizes[n][2] for n in files)
        data = sum(sizes[n][0] for n in files)
        strs = sum(sizes[n][1] for n in files)
        lcode = sum(sizes[n][2] for n in libs)
        ldata = sum(sizes[n][0] for n in libs)
        lstrs = sum(sizes[n][1] for n in libs)
        rld, esd, defs = module_rld(module)
        # An XLOAD image's def table (ccrel -x) has init's entry too.
        note_peak(limit_peaks, "MAX_DEFS", defs + 1, "linking " + prog)
        last = msg.strip().splitlines()[-1]
        m = re.search(r"shared names: (\d+) of \d+ slots, (\d+) of", last)
        if m:
            note_peak(limit_peaks, "MAX_GSYMS", int(m.group(1)),
                      "linking " + prog)
            note_peak(limit_peaks, "MAX_GNAMES", int(m.group(2)),
                      "linking " + prog)
        print("%s: %d files, and %d library objects:"
              % (prog, len(files), len(libs)))
        print("  its own code %d, data %d, strings %d: %d in all"
              % (code, data, strs, code + data + strs))
        print("  the library's code %d, data %d, strings %d: %d in all"
              " (%s)" % (lcode, ldata, lstrs, lcode + ldata + lstrs,
                         " ".join(sorted(libs))))
        print("  as a REL module: file %d bytes, all read into memory to"
              " load it;\n    %d of them RLD and %d ESD, freed after" % (
                  os.path.getsize(module), rld, esd))
        print("  %d functions: %d bytes of the loader's def table, kept"
              " (init's entry too, and an end byte)"
              % (defs, loader_sizes(module)[1]))
        print("  " + last.lstrip("; "))
        if raised:
            print("  (linked by a ccrel with raised limits, for %s)"
                  % ", ".join(raised))
        # Link again with a ccrel that has room for every static name,
        # for the call graph.
        big = big or build_big()
        named = os.path.join(OUT, prog + "-named")
        ok, msg = run([os.path.join(big, "ccrel"), "-v", "-o", named]
                      + objs)
        if os.path.exists(named):
            os.remove(named)
        if ok:
            funcs = parse_listing(msg)
            comps, edges, frame, unbounded = stack_report(funcs)
            static_report(funcs, comps, edges)
            with open(module + ".lst") as f:
                heap = sum(int(m.group(1)) for m in re.finditer(
                    r"^; heap global \d+: \S+, (\d+) bytes$", f.read(),
                    re.M))
            includes = peaks[[t[1] for t in PP_TABLES]
                             .index("MAX_INCLUDE")]
            # The form that runs on an Apple II (APPLE_LINK), linked
            # from the same objects.
            apple = module
            if APPLE_LINK:
                apple = module + ".apple"
                ok, msg = run([ccrel] + APPLE_LINK + ["-o", apple] + objs)
                if not ok:
                    print("  the Apple II form: link FAILED: %s"
                          % msg.strip().splitlines()[-1])
                    failed = True
                    continue
            if is_image(apple):
                r = image_fit(prog, apple, frame, unbounded, includes, xl)
                note_peak(limit_peaks, "MAX_DEFS", r["n"],
                          "linking " + prog)
                r["module"] = os.path.getsize(module)
            else:
                r = fit_report(prog, apple, rld + esd, heap, frame,
                               unbounded, includes)
                r.update(code=code + lcode, data=data + ldata,
                         strs=strs + lstrs, rld=rld, esd=esd,
                         body=os.path.getsize(module) - rld - esd)
            fit[prog] = r
    # Read by tools/limits.sh: keep this heading and the lines' form.
    print("\nPeaks: each limit, the most ccish's own source uses, and"
          " where:")
    for lim in sorted(limit_peaks):
        print("  %s %d %s" % (lim, limit_peaks[lim][0], limit_peaks[lim][1]))
    rows = [(p, fit.get(p)) for p, _ in PROGRAMS]
    if any(r and "fixups" in r for _, r in rows):
        image_summary(rows, xl)
    else:
        summary_table(rows)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

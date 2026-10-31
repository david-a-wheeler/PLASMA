#!/usr/bin/env python3
"""Measure ccish by compiling its own source with itself.

Usage: tools/measure.py [-full]   (after `make`; writes tools/out/)

Each file in src/ is preprocessed by gcc with the stand-in headers in
tools/inc/, as the small build (-DCCISH_SMALL, approach.md decision
21; or the full build, with -full), worked around where ccish still
lacks something, and compiled with ccpp, cccomp and ccgen. Then each
of the Apple II programs (ccpp, cccomp, ccgen, ccrel) is linked with
ccrel, with
tools/libstub.c
standing in for the C library. The report gives each file's code, data and string sizes
(from its .syms header) and each program's totals and link workload
(from ccrel's listing). See approach.md 18.5 and 18.6 E2.

The workarounds, each something ccish doesn't accept yet:
  - long and short become int, and l and L number suffixes go (ccish
    has unsigned, and u suffixes, but no long);
  - the printf family becomes 0 (in tools/inc/stdio.h), so its calls
    and the C library itself aren't counted;
  - main(argc, argv) is renamed main_args; libstub.c's main calls it;
  - nested braces in an initializer are dropped (C's brace elision
    gives the same values);
  - parse.c's one struct ?: becomes an if.
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
PP = ["ppname", "pplex", "ppout", "pp"]
NAM = ["nam"]                       # in both cccomp and ccgen
FRONT = ["tokin", "name", "type", "parse", "irout"]
BACK = ["irin", "lower", "peep", "emit", "helper"]
# Limits raised for big_cccomp(), and the fatal messages that call
# for it.
BIG_LIMITS = {"MAX_FUNCS": 1024, "MAX_VARS": 512, "MAX_LABELS": 512,
              "MAX_TYPES": 256, "MAX_PARAM_TYPES": 2048, "MAX_TAGS": 64,
              "MAX_MEMBERS": 512, "MAX_POOL": 32000, "MAX_IDS": 1024,
              "MAX_CASES": 256, "MAX_PP_POOL": 32000}
LIMIT_MSGS = ("too many", "is full")

# The Apple II programs (B3a). ccish, both passes in one, is for the
# host only: it is too large for one module.
PROGRAMS = [
    ("ccpp", ["ccpp"] + COMMON + PP),
    ("cccomp", ["cccomp"] + COMMON + NAM + FRONT),
    ("ccgen", ["ccgen"] + COMMON + NAM + BACK),
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
    text = re.sub(r"\b(long|short)\b", "int", text)
    text = re.sub(r"\bint int\b", "int", text)    # unsigned long int
    text = re.sub(r"\b(0[xX][0-9A-Fa-f]+|[0-9]+)([uU]?)[lL]+([uU]?)\b",
                  r"\1\2\3", text)
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


def compile_obj(bindir, name, src, keep=False):
    """Compile src into OUT/NAME.obj and .syms with BINDIR's ccpp,
    cccomp and ccgen (keeping NAME.i, NAME.nam and NAME.ir if keep);
    return (succeeded, messages)."""
    out = os.path.join(OUT, name)
    ok, msg = run([os.path.join(bindir, "ccpp"), "-o", out, src])
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
    for name in os.listdir(os.path.join(CCOMP, "src")) + ["../ccish.msg"]:
        with open(os.path.join(CCOMP, "src", name)) as f:
            text = f.read()
        if name == "ccish.h":
            for limit, value in BIG_LIMITS.items():
                text = re.sub(r"(#define %s\s+)\d+" % limit,
                              r"\g<1>%d" % value, text)
        if name == "link.c":
            # Room for every static name, so the listing names them all.
            text = re.sub(r"(#define MAX_GNAMES\s+)\d+", r"\g<1>32000", text)
        with open(os.path.join(big, os.path.basename(name)), "w") as f:
            f.write(text)
    src = lambda names: [os.path.join(big, n + ".c") for n in names]
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccpp")]
                   + src(["ccpp"] + COMMON + PP), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "cccomp")]
                   + src(["cccomp"] + COMMON + NAM + FRONT), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccgen")]
                   + src(["ccgen"] + COMMON + NAM + BACK), check=True)
    subprocess.run(["cc", "-O2", "-o", os.path.join(big, "ccrel")]
                   + src(["ccrel"] + COMMON + ["link"]), check=True)
    return big


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
    with open(os.path.join(CCOMP, "src", "ccish.h")) as f:
        max_nest = int(re.search(r"#define MAX_NEST\s+(\d+)",
                                 f.read()).group(1))
    return names, max_nest


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
        keep = name == "parse"
        ok, msg = compile_obj(CCOMP, name, src, keep)
        if not ok and any(m in msg for m in LIMIT_MSGS):
            over[name] = msg.strip().split(": ", 1)[-1]
            big = big or build_big()
            ok, msg = compile_obj(big, name, src, keep)
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
            tok_sizes = (os.path.getsize(src), os.path.getsize(base + ".i"),
                         os.path.getsize(base + ".nam"))
            for suffix in (".i", ".nam", ".ir"):
                os.remove(base + suffix)

    if tok_sizes:
        print("\nparse.c: %d bytes preprocessed by gcc -E -P; ccpp's token"
              " stream %d, name file %d" % tok_sizes)
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

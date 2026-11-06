#!/usr/bin/env python3
"""Compare the VM ops two ccish trees' code runs, test by test.

Usage: tools/opcount.py BASE [-v]

BASE is another ccish directory, already built (make all): usually a git
worktree of an earlier commit, at a short path, since ccish's output
names are limited to 128 characters, e.g.

    git worktree add --detach /tmp/wb HEAD
    make -C /tmp/wb/ccish all
    tools/opcount.py /tmp/wb/ccish
    git worktree remove --force /tmp/wb

Run from this ccish directory, after make all. It builds a copy of plvm
that counts each opcode it dispatches (tools/out/cvm/plvmc: the VM's
sources from ../src/vmsrc, patched, never changed in place), compiles
each tests/run/*.c with BASE's ccish and with this tree's, runs both
under it, and prints, for each test whose op count changed, the two
totals and the change; then the totals over those tests. A test whose
output differs between the two is reported as OUTPUT DIFFERS (which
should never happen for an optimization). With -v, each changed test's
counts by opcode follow.

The counts are of ops executed, not cycles: an LAW and an LA count the
same, though an LAW takes about 24 cycles more on the 6502.
"""
import os
import shutil
import subprocess
import sys

CCOMP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(CCOMP, "tools", "out")
VMSRC = os.path.join(CCOMP, "..", "src", "vmsrc")

# The counter: before each dispatch, count the opcode; at exit, write
# the counts (hex opcode, count), then the total, to opcounts.txt in
# the current directory.
COUNTER = r'''unsigned long opcount[256];
void dump_counts_ext(void)
{
    unsigned long t = 0;
    int i;
    FILE *f = fopen("opcounts.txt", "w");
    for (i = 0; i < 256; i++)
        if (opcount[i]) { t += opcount[i]; fprintf(f, "%02X %lu\n", i, opcount[i]); }
    fprintf(f, "total %lu\n", t);
    fclose(f);
}
'''


def patch(path, old, new):
    """Replace old, which must occur once in file path, by new."""
    with open(path) as f:
        text = f.read()
    if text.count(old) != 1:
        sys.exit("opcount.py: can't patch %s: the VM has changed" % path)
    with open(path, "w") as f:
        f.write(text.replace(old, new))


def build_vm():
    """Build the op-counting plvm from a copy of the VM's sources, and
    return its path."""
    subprocess.run(["make", "-s", "-C", os.path.join(CCOMP, "..", "src"),
                    "plvm"], check=True)
    cvm = os.path.join(OUT, "cvm")
    shutil.rmtree(cvm, ignore_errors=True)
    shutil.copytree(VMSRC, os.path.join(cvm, "vmsrc"))
    plvm = os.path.join(cvm, "vmsrc", "plvm.c")
    patch(plvm, "int trace = 0;", "int trace = 0;\n" + COUNTER)
    patch(plvm, "        switch (*vm_ip++)\n",
          "        opcount[*vm_ip]++;\n        switch (*vm_ip++)\n")
    cmdsys = os.path.join(cvm, "vmsrc", "cmdsys.c")
    patch(cmdsys, "int main(int argc, char **argv)\n{\n",
          "extern void dump_counts_ext(void);\n"
          "int main(int argc, char **argv)\n{\n"
          "    atexit(dump_counts_ext);\n")
    with open(cmdsys) as f:
        text = f.read()
    with open(cmdsys, "w") as f:
        f.write("#include <stdlib.h>\n" + text)
    src = [os.path.join("vmsrc", n) for n in
           ("plvm.c", "cmdsys.c", "sysio.c", "a1cffa.c",
            os.path.join("lib6502", "lib6502.a"))]
    subprocess.run(["cc", "-w", "-o", "plvmc"] + src, cwd=cvm, check=True)
    return os.path.join(cvm, "plvmc")


def run(ccish, src, where, vm):
    """Compile src with ccish into directory where, run it under vm, and
    return (its output, {opcode: count}, total), or None if it didn't
    compile."""
    name = os.path.splitext(os.path.basename(src))[0].lower()
    mod = os.path.join(where, name)
    r = subprocess.run([ccish, "-o", mod, src], cwd=CCOMP,
                       capture_output=True)
    if r.returncode != 0:
        return None
    counts = os.path.join(where, "opcounts.txt")
    if os.path.exists(counts):
        os.remove(counts)
    try:
        r = subprocess.run([vm, name], cwd=where, capture_output=True,
                           timeout=20)
        output = r.stdout
    except subprocess.TimeoutExpired:
        output = b"(timed out)"
    ops, total = {}, 0
    if os.path.exists(counts):
        with open(counts) as f:
            for line in f:
                key, value = line.split()
                if key == "total":
                    total = int(value)
                else:
                    ops[int(key, 16)] = int(value)
    return output, ops, total


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    base = os.path.abspath(args[0])
    vm = build_vm()
    dirs = {}
    for which in ("base", "new"):
        dirs[which] = os.path.join(OUT, "ops", which)
        shutil.rmtree(dirs[which], ignore_errors=True)
        os.makedirs(dirs[which])
    tests = sorted(f for f in os.listdir(os.path.join(CCOMP, "tests", "run"))
                   if f.endswith(".c"))
    sums = [0, 0]
    changed = differ = 0
    for t in tests:
        src = os.path.join(CCOMP, "tests", "run", t)
        b = run(os.path.join(base, "ccish"), src, dirs["base"], vm)
        n = run(os.path.join(CCOMP, "ccish"), src, dirs["new"], vm)
        if b is None or n is None:
            print("%-20s doesn't compile with %s" %
                  (t, "BASE" if b is None else "this tree"))
            continue
        if b[0] != n[0]:
            print("%-20s OUTPUT DIFFERS" % t)
            differ += 1
        if b[2] == n[2]:
            continue
        changed += 1
        sums[0] += b[2]
        sums[1] += n[2]
        print("%-20s %8d %8d %+7.1f%%" %
              (t, b[2], n[2], (n[2] - b[2]) * 100.0 / max(b[2], 1)))
        if verbose:
            for op in sorted(set(b[1]) | set(n[1])):
                x, y = b[1].get(op, 0), n[1].get(op, 0)
                if x != y:
                    print("    op $%02X  %6d -> %6d" % (op, x, y))
    print("%d of %d tests changed; %d with different output" %
          (changed, len(tests), differ))
    if changed:
        print("together: %d -> %d ops (%+.1f%%)" %
              (sums[0], sums[1], (sums[1] - sums[0]) * 100.0 / sums[0]))
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())

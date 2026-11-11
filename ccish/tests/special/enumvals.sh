#!/bin/sh
# Check the enum-value rule (doc/enums.md): the enums' values are
# assigned for the set tests and value manipulations on them, and may
# be reassigned; each test and manipulation is a #define with a
# _Static_assert of what it relies on. This checks:
# 1. In src/*.c, a hex number, or a decimal of 16 or more, in a bit
#    or shift op (op | 0x20, (op & 0x1F) == 0) must be a value mask
#    (a byte of a value, a sign bit: the list in the program below),
#    or on a line of a _Static_assert, or on a line of a #define
#    whose block a _Static_assert follows (blanks between). A raw
#    number in a .c's bit op assumes the assignment, and breaks
#    silently when the values move.
# 2. In src/*.c, arithmetic on a named constant (tok == TC_CASE + 1,
#    names[op - IR_ADD]) must be in such a pair, or be one of the
#    existing uses in the list in the program below, each noted with
#    where its relationship is pinned. A combination of two named
#    constants (OP_LB | MOP_VOLATILE) names both, and is allowed.
# 3. Each src/*.h that defines a set test or bit manipulation on an
#    enum (an IN_BLOCK, or a #define whose body is a bit op) must
#    name doc/enums.md (the ENUM VALUES banner) and hold a
#    _Static_assert.
# Comparisons with a named constant (tok == TC_IF) name one value only,
# and are allowed. Character codes (';', '*') are fixed by C, not
# assigned, and need no define; a value mask (0xFF, 0x8000)
# manipulates a value, not an enum.

set -u
export LC_ALL=C
bad=0
for f in src/*.c; do
    case $(basename "$f") in ,*) continue ;; esac
    awk '
    function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
    {
        raw[NR] = $0
        t = $0
        if (inbc) {
            if ((p = index(t, "*/")) > 0) { inbc = 0; t = substr(t, p + 2) }
            else t = ""
        } else {
            for (;;) {
                p = index(t, "/*")
                if (p == 0) break
                q = index(substr(t, p + 2), "*/")
                if (q == 0) { t = substr(t, 1, p - 1); inbc = 1; break }
                t = substr(t, 1, p - 1) substr(t, p + q + 3)
            }
        }
        p = index(t, "//")
        if (p > 0) t = substr(t, 1, p - 1)
        clean[NR] = t
        c = trim(t)
        if (c ~ /^#define/) defstart[NR] = 1
        indef[NR] = (defstart[NR] != 0) ? 1 \
            : (indef[NR-1] && raw[NR-1] ~ /\\[ \t]*$/) ? 1 : 0
        if (indef[NR]) dstart[NR] = (defstart[NR] != 0) ? NR : dstart[NR-1]
        if (c ~ /^_Static_assert/) { afrom = NR; inassert[NR] = 1 }
        else if (afrom != 0) { inassert[NR] = 1; if (c ~ /;[ \t]*$/) afrom = 0 }
        if (c ~ /^enum[ \t]*\{/) { efrom = NR; inenum[NR] = 1 }
        else if (efrom != 0) { inenum[NR] = 1; if (c ~ /};[ \t]*$/) efrom = 0 }
    }
    END {
        # A #define block is covered if a _Static_assert is the next
        # thing after it (comments and blanks already stripped).
        for (i = 1; i <= NR; i++) {
            if (defstart[i]) {
                j = i
                while (j <= NR && indef[j]) j++
                k = j
                while (k <= NR && trim(clean[k]) == "") k++
                covered[i] = (k <= NR && trim(clean[k]) ~ /^_Static_assert/) \
                    ? 1 : 0
                i = j - 1
            }
        }
        # Rule 1: a raw number in a bit or shift op.
        for (i = 1; i <= NR; i++) {
            s = clean[i]
            while (match(s, /0[xX][0-9A-Fa-f]+[uU]?|[0-9]+/)) {
                lit = substr(s, RSTART, RLENGTH)
                pre = substr(s, 1, RSTART - 1)
                post = substr(s, RSTART + RLENGTH)
                sub(/[ \t]+$/, "", pre)
                a = substr(pre, length(pre), 1)
                b = (length(pre) > 1) ? substr(pre, length(pre) - 1, 1) : ""
                sub(/^[ \t]+/, "", post)
                c1 = substr(post, 1, 1)
                c2 = substr(post, 2, 1)
                ctx = 0
                if ((a == "&" && b != "&") || (a == "|" && b != "|") \
                    || a == "^" || a == "~")
                    ctx = 1
                else if ((a == "<" || a == ">") && a == b)
                    ctx = 1
                else if ((c1 == "&" && c2 != "&") || (c1 == "|" && c2 != "|") \
                         || c1 == "^")
                    ctx = 1
                else if ((c1 == "<" || c1 == ">") && c1 == c2)
                    ctx = 1
                if (ctx) {
                    v = tolower(lit)
                    sub(/[uU]$/, "", v)
                    ok = (v == "0xf" || v == "0xff" || v == "0xfff" \
                          || v == "0xffff" || v == "0xff00" || v == "0x7fff" \
                          || v == "0x8000" || v == "0x80")
                    if (!ok && v !~ /^0x/ && v + 0 < 16) ok = 1
                    if (!ok && inassert[i]) ok = 1
                    if (!ok && indef[i] && covered[dstart[i]]) ok = 1
                    if (!ok) {
                        print FILENAME ":" i ": " lit ", in a bit or shift op:"
                        print "    the enum values may be reassigned; use a"
                        print "    named constant, or a #define with a _Static_assert"
                        print "    (doc/enums.md)"
                        bad = 1
                    }
                }
                s = post
            }
        }
        # Rule 2: arithmetic on a named constant. The existing uses,
        # each with where its relationship is pinned:
        nex = 0
        # IR_BUF is a buffer size (#define), not an enum.
        exf[nex] = "src/irin.c"; exr[nex] = "lower_held\\(IR_BUF / 2\\)"; nex++
        # A tag is a byte: pinned by the assert on IR_BYTE_OP in ir.h.
        exf[nex] = "src/irin.c"; exr[nex] = "IR_BYTE_OP \\+ IR_TAG_END"; nex++
        # TY_UINT == TY_INT + 1: pinned by the assert in type.h.
        exf[nex] = "src/parse.c"; exr[nex] = "TY_INT \\+ \\(IS_UNS\\(a\\) \\| IS_UNS\\(b\\)\\)"; nex++
        # Guarded by IR_IS_BINARY; the block anchor and alignment are
        # pinned by the asserts in ir.h.
        exf[nex] = "src/pexpr.c"; exr[nex] = "names\\[op - IR_ADD\\]"; nex++
        # The message numbers of the keywords and the operators are by
        # offset in their blocks (the format, CCPP.md); tokin.c asserts
        # the ranges.
        exf[nex] = "src/tokin.c"; exr[nex] = "MSG_OPS \\+ \\(tok - TC_OPS\\)|MSG_KEYWORDS \\+ \\(tok - TC_KEYWORDS\\)|TC_KEYWORDS \\+ NKEYWORDS|TC_OPS \\+ NOPS"; nex++
        # The ccpp stream codes, and the spellings, by offset in their
        # blocks (the format, CCPP.md).
        exf[nex] = "src/pplex.c"; exr[nex] = "return TC_KEYWORDS \\+ i|return TC_OPS \\+ i|TC_KEYWORDS \\+ NKEYWORDS|TC_OPS \\+ NOPS|keywords\\[code - TC_KEYWORDS\\]|ops\\[code - TC_OPS\\]"; nex++
        # The stream keyword codes, by offset in their block (CCPP.md).
        exf[nex] = "src/ppif.c"; exr[nex] = "TC_KEYWORDS \\+ NKEYWORDS"; nex++
        # TK_PTR is assigned TY_UINT + 1 in the enum in type.c, asserted there.
        exf[nex] = "src/type.c"; exr[nex] = "ntypes = TY_UINT \\+ 1"; nex++
        re1 = "(^|[^A-Za-z0-9_])(IR_|T_|TC_|OP_|TY_|TK_|MOP_|Q_|FIX_|SYM_|REL_|VL_|FN_|REC_|D_|ID_|HELP_)[A-Z][A-Z_0-9]*[ \t]*[-+*/]"
        re2 = "[-+*/][ \t]*(IR_|T_|TC_|OP_|TY_|TK_|MOP_|Q_|FIX_|SYM_|REL_|VL_|FN_|REC_|D_|ID_|HELP_)[A-Z][A-Z_0-9]"
        for (i = 1; i <= NR; i++) {
            if (inassert[i] || indef[i] || inenum[i]) continue
            t = clean[i]
            if (t ~ re1 || t ~ re2) {
                ex = 0
                for (e = 0; e < nex; e++)
                    if (exf[e] == FILENAME && t ~ exr[e]) { ex = 1; break }
                if (!ex) {
                    print FILENAME ":" i ": arithmetic on a named constant:"
                    print "    the enum values may be reassigned; hide the"
                    print "    test or turn in a #define with a _Static_assert,"
                    print "    or note where its relationship is pinned"
                    print "    (doc/enums.md)"
                    bad = 1
                }
            }
        }
        exit bad + 0
    }' "$f" || bad=1
done
for f in src/*.h; do
    case $(basename "$f") in ,*) continue ;; esac
    awk '
    function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
    { raw[NR] = $0 }
    END {
        hasenum = 0; hasbit = 0; hasassert = 0; hasbanner = 0
        i = 1
        while (i <= NR) {
            c = trim(raw[i])
            if (c ~ /^enum[ \t]*\{/) hasenum = 1
            if (c ~ /^#define/) {
                j = i
                body = raw[i]
                while (raw[j] ~ /\\[ \t]*$/ && j < NR) { j++; body = body " " raw[j] }
                b = body
                gsub(/&&/, "", b); gsub(/\|\|/, "", b)
                if (b ~ /IN_BLOCK|&|\^|<<|>>|[|]/) hasbit = 1
                i = j
            }
            if (c ~ /^_Static_assert/) hasassert = 1
            if (raw[i] ~ /doc\/enums\.md/) hasbanner = 1
            i++
        }
        if (hasenum && hasbit && (!hasbanner || !hasassert)) {
            print FILENAME ": defines a set test or bit manipulation on an enum:"
            print "    add the ENUM VALUES banner (doc/enums.md) and the _Static_asserts"
            bad = 1
        }
        exit bad + 0
    }' "$f" || bad=1
done
exit $bad

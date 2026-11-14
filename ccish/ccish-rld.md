# What cccomp's RLD is made of

A companion to `ccish-size-options.md`, section 7: why the RLD has the
entries it does, and whether it can come down. All numbers are
measured on this tree's small build (`tools/measure.py`'s
`tools/out/cccomp`, 42,613 bytes; the RLD is 3,082 four-byte entries
plus the terminator and a 69-byte ESD, 12,398 of the file).

## How entries get there

The compiler writes no RLD. It writes **relocation records** in the
object's `F` records (OBJECT.md): one per word of bytecode that needs
a final address, with a kind (1 data, 2 function, 3 string, 4 data
symbol, 5 helper) and target. `ccgen` adds one only for an absolute
word (emit.c's add_reloc: LA, a data CW, CALL, and the absolute forms
LAB/LAW/SAB/SAW/DAB/DAW, in every fix but FIX_NONE). A **branch
never gets one**: emit.c's branch_word() writes the final relative
offset at compile time (the label is within the same function:
`code_word(label_word[n] - nfbuf)`), and a relative offset stays
correct at any load address, since branch and target move together.
`cclink` (link.c) then turns each record into an RLD entry: 0x81 for
an internal target, 0x91 for an import (the ESD index in the entry's
byte), plus one 0x02 entry per defined function (its offset, for the
VM's def dictionary, which maps a function's offset to its address
for the module's entry and for external code; an ordinary internal
CALL carries its already-fixed absolute word), and a few more
for the init function cclink assembles itself (its calls and its
pointers to the moved globals). The VM's loader (cmdsys.c's load_mod)
applies every entry once, at the load, and frees the RLD after.

## The breakdown (3,082 entries)

| Entries | What | Share |
|---|---|---|
| 1,712 + 5 + 6 | calls: kind-2 relocations (to functions of the module), kind-5 (ccgen's helpers' CALLs, whose own code has no records), and the 6 CALLs of the init function cclink assembles | 56% |
| 627 + 327 + 12 | absolute references to data: kind-1 (the globals' data: the fn_* tables, id_nm, tok_text, the in/out buffers), kind-4 (data symbols, extern globals most of which resolve inside the module), and the init function's 11 DAW and 1 LAW | 31% |
| 58 + 36 | string addresses: kind-3 in the code (58) plus the 36 R records in the data (a `char *p = "..."`; also kind 3) | 3% |
| 299 | 0x02 function definitions: 296 object functions plus the 3 helpers used (udiv, umod, ushr) | 10% |

Of the 2,783 fixups, 46 are imports, written 0x91 (the object
code's calls of the VM's shared names -- putc, puti, putln, ... --
and the init's heap calls); the rest are 0x81. Of the 3,082, 18 are
the init function's words (its 6 CALLs plus the 12 data pointers).
Check: 1,712 + 5 + 6 + 627 + 327 + 12 + 58 + 36 + 299 = 3,082, and
the objects' records plus the R records (2,729 + 36) plus the init
(18) plus the definitions (299) give the same total. The question
"does a relative branch need an entry?" is answered by the count:
ccish's own code has 1,397 branch words (BRFLS 489, BRNCH 327, BRNE
249, BRTRU 203, BREQ 104, SEL 15, the counted loops) and they take
**0** of the 3,082 -- branch_word() in emit.c writes the final
relative offset and adds no record, so the current design is already
the form that needs no RLD, with no size or time cost.

Two cross-checks from the listing (tools/out/cccomp.lst): 1,723 CALL
ops (the 11 over the kind-2 records are the init's and helpers'
calls, whose code is in the module but has no F records), and 977
LA/LAW/SAW/DAW/LAB/SAB ops plus the data CWs (the overage over the
627 + 327 kind-1/4 records is the CWs that hold a constant, not an
address: FIX_NONE, no record).

## What is not in there: the messages

The doc's A3 idea assumed "one LA of the message per call site (and
so one RLD entry per site)". That is not what the code does: a
diagnostic's message number is an **immediate** (`error(34, NULL)`
emits 34 as a constant), and the text is read from the message file
**at run time** (diag.c's msg_text() fopens ccish.msg and scans for
the number, falling back to the bare number if the file is missing).
The 94 kind-3 entries are the genuine string literals the source
embeds (the usage text, the keyword and operator spellings for
tokin's messages, ...), not the 303 messages. So merging messages
(A3) would touch the .msg file -- which is not part of the module --
and the immediates in the code: it buys nothing in the module file
or the RLD. A3 is downgraded from "buys the least" to "buys nothing
for the size goal".

## Can it come down?

There is no obvious cheap way to cut the RLD significantly. In size
order:

- **A relative CALL** would remove the call entries (about 1,723,
  56%, about 6.9K of the file). The opcode set's only call is
  `CALL <absolute word>` (mop.h's branch-and-call family: only the
  BR* ops are relative), so this is a VM opcode change in every VM
  (plvm.c, the 6502 ROMs, the loaders) -- a project, not a fix, and
  it changes the call-dispatch design the def dictionary (the 299
  entries) exists to serve.
- **Base-plus-index for repeated table references** (one LA per
  function-and-table, then ADDI offset and LW -- the peep already
  fuses ADD/LW to ADDLW/IDXLW where the base is a local) is code at
  about even (three bytes either way, up to one more where the offset
  exceeds ADDI's 255) but drops the 954+ per-reference entries
  (about 4K). It is a ccgen change, allowed by the cost policy, but
  it is a project in its own right: the tables' 255-byte windows,
  the sign of the offsets, the interaction with the fused forms, and
  it would change the generated code of every program, for the data
  references in them.
- **The 299 function-definition entries** are the call-dispatch
  design; they do not come down without changing how CALL resolves.
- The 46 imports and the 18 init words are the program's interface
  to the VM; not a lever.

Against the 40K goal, `ccish-size-options.md` section 7 remains the
better first move: streaming the RLD at the load (the VM reads the
four-byte entries one at a time instead of holding them) removes the
whole 12,398 from the load peak without changing cccomp at all,
whereas every lever above shrinks the file itself, which the load
only needs if a given VM cannot stream.

## Reproducing the numbers

- The RLD: `python3` over `tools/out/cccomp` -- the header (19
  bytes: size, magic $6502, sysflags, bytecode, defcnt, init, the
  dependency names), then the body; the RLD is at file offset
  modsize + 2, four-byte entries (type, word, byte) until a type of
  0, then the ESD.
- The per-kind records: parse each `tools/out/NAME.obj` for the
  cccomp program's sources (cccomp, nam, tokin, name, type, parse,
  irout, diag, bio, libstub): the `F` records' relocation lists
  (kind, offset, target; 5 bytes each) and the `R` records in the
  data (kind 1, 3 or 4; the addend only for kind 4). D records have a
  one-byte count; Z, S, R and F lengths are words.
- The listing cross-check: count the mnemonics in
  `tools/out/cccomp.lst`; the `(init):` section at the end is
  cclink's, and has 6 CALL, 11 DAW and 1 LAW.

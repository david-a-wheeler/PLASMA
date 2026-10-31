# ccish stream files (object format)

The compiler proper (stages 1–4) doesn't write the REL module itself.
Its front end (`cccomp`) writes an **IR file**, its back end (`ccgen`)
turns that into a **stream file** and a **symbol summary**, and a
separate
**final pass** (`src/link.c`, the program `ccrel`) turns them into the
module. See [approach.md](approach.md), 18.1, for why. Together the
two files are ccish's object file format, an **object**. The final
pass is also a linker, which links several objects into one module
(issue 11), so this format should change only deliberately, with the
version number bumped.

For output `NAME`, the files are `NAME.obj` and `NAME.syms`. `ccish`
removes them after the module is written, and after any error, unless
`-k` is given; `ccgen` leaves them for `ccrel`, which keeps them. This
is format version 6, which adds zero-filled globals (approach.md,
"Large zero-filled data on the heap"): a `Z` record for a run of zero
bytes, the summary's zero-filled size, and data symbol flags 64 and 128, so that
`ccrel` can move large zero-filled globals to the heap (below,
"Zero-filled globals"). Version 5 added helper functions (approach.md,
"`unsigned` and helper functions design"): code `ccgen` writes into
each object that calls it (an `H` record, below), of which `ccrel`
keeps one copy. Version 4 added what linking several files needs
(approach.md issue 11): data symbols, local (`static`) symbols, a
relocation to an `extern` global, and type hashes; version 3 had none
of these. M5's version 2 had four stream
files (`.data`, `.drel`, `.strs` and `.code`), which version 3 made
records in the one stream file, so that each pass has only one input
and one output file open (PLASMA's Apple II file library allows
4 open files; approach.md 18.6 B6a).

All words are 16-bit little-endian. Every file is written once, in
order, with no seeking, through 512-byte block buffers (`src/bio.c`).

The IR file, `NAME.ir` (below), passes from `cccomp` to `ccgen`
(approach.md 18.7); `ccgen` turns it into `NAME.obj` and `NAME.syms`.
Before it, `ccpp` turns the source into the files `cccomp` reads, the
token stream `NAME.i` and the name file `NAME.nam`: see
[CCPP.md](CCPP.md).

## `NAME.obj`: the stream

A sequence of records, until end of file, in the order the compiler
made them: data, data relocations and strings as the source declares
them, and each function when it ends, so the kinds are interleaved.
Each record starts with a one-byte tag, a letter:

| Tag | Record |
|-----|--------|
| `D` | a run of data bytes |
| `R` | a data word that holds an address |
| `Z` | a run of zero data bytes |
| `S` | a string literal |
| `F` | a function |
| `H` | a helper function |

The `D`, `R` and `Z` records, in order, make up the data (the global
variables); the `S` records, in order, make up the string pool; and
the `F` records, in order, make up the code. The final pass reads the
stream three times, once for each, skipping the other records, and a
fourth time for the `R` records and the `F` records' relocations (and
the `D` and `Z` records, to know each `R` record's offset),
which give the RLD's relocations. The RLD's function entries come from
the `.syms` files (each defined function's offset).

### `D`: data bytes

| Size | Field |
|------|-------|
| byte | tag `D` |
| byte | count *n* (1–128) |
| *n* bytes | the next bytes of the data |

The global variables' bytes, in the order they appear in the source:
each global's initial value, or zero bytes of its size if it has none
(a run of 4 or more zeros is a `Z` record instead). A long run is split
into several records. A large zero-filled global isn't in the data at
all (below, "Zero-filled globals").

### `Z`: zero bytes

| Size | Field |
|------|-------|
| byte | tag `Z` |
| word | count *n* |

The next *n* bytes of the data are 0: a global with no initializer, or
the zeros that end an initializer (`char buf[100] = "hi";`).

### `R`: an address in the data

| Size | Field |
|------|-------|
| byte | tag `R` |
| byte | kind: 1 = data address, 3 = string address, 4 = data symbol address |
| word | target: a data offset (kind 1), a string pool offset (kind 3), or a data symbol index (kind 4) |
| word | kind 4 only: the addend, bytes added to the symbol's address |

It stands for the next two bytes of the data (`char *p = "hi";`,
`int *q = &x;`), so its offset is where it falls among the `D` and `R`
records. The final pass writes the address there, and adds an RLD entry
so that the loader can adjust it. Kind 4 is the address of a global
declared `extern` and not (yet) defined in this file, or of a zero-filled one,
plus the addend (`int *q = &ext[3];`).

Data offsets elsewhere count from the first byte of the data. Its
length is the data size.

### `S`: a string literal

| Size | Field |
|------|-------|
| byte | tag `S` |
| word | length *n* |
| *n* bytes | the characters |

Every string literal, in the order the compiler met them. In the pool,
each is `[length][chars][NUL]` (issue 8), *n* + 2 bytes; a length over
255 is written as 0. String offsets count from the first byte of the
pool, and its length is the string pool size.

The pool is kept apart from the data so that string literals in a
global's initializer (`char *names[] = {"zero", "one"};`) don't land
in the middle of the global, while both are still written as they are
parsed. In the module, the pool comes right after the globals.

### `F`: a function

| Size | Field |
|------|-------|
| byte | tag `F` |
| word | symbol index of the function (into `NAME.syms`) |
| word | code length *L* |
| *L* bytes | the function's bytecode |
| word | relocation count *R* |
| *R* × 5 bytes | relocation records |

Each relocation record marks a word in this function's bytecode that
needs a final address:

| Size | Field |
|------|-------|
| byte | kind: 1 = data address, 2 = function address, 3 = string address, 4 = data symbol address, 5 = helper address |
| word | offset of the word, from the start of this function |
| word | target: a data offset (kind 1), a function symbol index (kind 2), a string pool offset (kind 3), a data symbol index (kind 4), or a helper's number (kind 5) |

The word itself is written as 0, except for kind 4, where it holds the
addend; the final pass fills it in. A function address may refer to a
function defined later in the file, or to an import, which the loader
fills in. A data symbol address (kind 4) refers to a global declared
`extern`, used before any definition in this file, or to a zero-filled
global (in code, its word is always the operand of an `LA` with addend
0, which `ccrel` may make an `LAW`: "Zero-filled globals", below): a
reference to another global with storage here is kind 1.

The function's code offset in the code segment is the sum of the
lengths of the `F` records before it. The frame sizes in `ENTER` and
`LEAVE` are already filled in, and so are branch offsets: branches
stay within a function, are relative, and are resolved in the
compiler's function buffer, so they need no records here.

### `H`: a helper function

| Size | Field |
|------|-------|
| byte | tag `H` |
| byte | the helper's number: 0 udiv, 1 umod, 2 ushr |
| word | code length *L* |
| *L* bytes | its bytecode |

Unsigned arithmetic that PLASMA has no opcode for: `n / d`, `n % d`
and `n >> c`, with the two operands as its parameters (the call is
`CALL` with a kind 5 relocation). `ccgen` writes one, after the
object's functions, for each helper the object calls. A helper has no
relocations and isn't part of the object's code segment: `ccrel`
keeps the first object's copy of each and places it after every
object's code, and every object's copy must be the same (the summary
gives its size and checksum).

## `NAME.syms`: the symbol summary

Written last, when compilation ends. The final pass reads it first, so
that every address is known before anything is written. It is a file
of its own so that the compiler needn't seek back to patch a header.

| Size | Field |
|------|-------|
| 2 bytes | magic `CI` |
| byte | format version: 6 |
| word | data size |
| word | string pool size |
| word | code size (the total code length of the `F` records) |
| word | zero-filled size: the total size of the data symbols with flag 64 defined here |
| word | function symbol count *N* |
| word | data symbol count *M* |
| *N* entries | function symbols, in index order |
| *M* entries | data symbols, in index order |
| byte | helper count *H* |
| *H* entries | the helpers in the stream, by number |

Each helper entry:

| Size | Field |
|------|-------|
| byte | its number |
| word | its code length |
| word | a checksum of its code: `h = (h * 31 + byte) & 0xFFFF`, from 0 |
| byte | name length *n* (1-31) |
| *n* bytes | name, for the listing (`udiv`) |

Each function symbol entry:

| Size | Field |
|------|-------|
| byte | flags: 1 = defined here, 2 = called, 4 = returns a value, 8 = local (`static`), 16 = a CMDSYS function, 32 = a CMDSYS name declared differently |
| byte | parameter count, or 255 if unknown |
| word | code offset, if defined; otherwise 0 |
| word | type hash, or 0 if the parameters are unknown (`int f();`) |
| byte | name length *n* (1–31) |
| *n* bytes | name |

Each data symbol entry (a global variable, numbered as the compiler
numbers them; every global is listed, static locals aren't):

| Size | Field |
|------|-------|
| byte | flags: 1 = defined here, 2 = used, 8 = local (`static`), 64 = zero-filled, 128 = its address is in this object's data |
| word | data offset, if defined (with flag 64, an offset in the zero-filled space instead); otherwise 0 |
| word | size in bytes, or 0 if unknown (`extern int a[];`) |
| word | type hash |
| byte | name length *n* (1–31) |
| *n* bytes | name |

A symbol without flag 8 is shared by every object linked together, by
its name; one with flag 8 belongs to its object alone. A function that
is called but defined in no object is an import if every object's
entry for it has flag 16, the CMDSYS functions (declared from the
compiler's table, or matching it). Otherwise, it and a global that is
used but defined nowhere are undefined references, reported by the
final pass ("undefined reference to"), or message 173 for flag 32.
Nothing is exported: names stay inside the module. A global declared
`extern` that nothing uses needn't be defined anywhere.

**The type hash** lets a linker check that files agree on a function's
or global's type. It is `h = (h * 31 + byte) & 0xFFFF`, from 0, over a
canonical string of the type: `i` int (and enums), `c` char, `v` void,
`s`, the tag's name and `;` for a struct, `k` (const) and `V`
(volatile) before what they qualify, `*` then the pointed-to type, and
`[`, the count (low byte, then high), then the element type. A
function's is its return type, `(`, each parameter's type (without its
own qualifiers), then `)`. A data symbol's leaves out its outermost
array count (`[` then the element type), so that `extern int a[];`
matches `int a[10];`; the size field covers that. A hash that comes
out 0 is written as 1.

**Zero-filled globals.** A global array or struct with no initializer,
of at least `BSS_MIN` (256) bytes, isn't in the data: it has flag 64,
and its offset is in the object's zero-filled space, whose size the
summary gives. Every reference to it, in code and data, is a kind 4
relocation, to its symbol. A global declared `extern` may get flag 64
from the object that defines it. Flag 128 says that an initializer in
this object holds its address (`int *p = big + 3;`), or, for an
`extern` one, may.

## `NAME.ir`: the IR file

Written by `cccomp` (the front end) and read once, in order, by
`ccgen` (the back end), which writes `NAME.obj` and `NAME.syms` from
it (approach.md 18.7). It holds the IR ops as the parser produced them
(after any reordering in the IR buffer), whose meanings are in
[IR.md](IR.md); the data records, as in `NAME.obj`; and the symbol
summary. It is an intermediate file: `ccgen` removes it after success,
unless `-k`.

Names in it are ids (version 3): numbers into the name file,
`NAME.nam`, which `ccpp` wrote with the token stream ([CCPP.md](CCPP.md)).
`ccgen` reads the name file too, to spell the names in `NAME.syms` and
its messages, and removes it after success, unless `-k`. The file
starts with a header:

| Size | Field |
|------|-------|
| 3 bytes | magic `CIR` |
| byte | IR format version: 4 |
| word | the name file's stamp: its id count |
| word | and its checksum |

`ccgen` checks the name file as `cccomp` does (CCPP.md, "What `cccomp`
checks"), and that its stamp is this one, before it reads any name;
otherwise it stops with message 232, "name file is stale or damaged".
The source file's name, for its messages, is the name file's file 1.
(Version 2 had the source file's name here, and spelled every name in
the file.)

Then records, until the summary, which is last. Each starts with a
one-byte tag: a capital letter for most records, as in `NAME.obj`,
and for an IR op a byte below `$40` or from `$80` to `$BF`:

| Tag | Record |
|-----|--------|
| `D`, `R`, `Z`, `S` | data bytes, an address in the data, zero bytes, a string literal: exactly as in `NAME.obj`, which `ccgen` copies them to whole |
| `L` | a source line |
| `N` | a function's name |
| `W` | prefix: the next op's operand *a*, as a word |
| `X` | prefix: the next op's second operand *f* |
| `Y` | the symbol summary; the last record |
| `$00`-`$3F` | an IR op, with no one-byte operand |
| `$80`-`$BF` | an IR op plus `$80`, followed by a one-byte operand *a* |

### IR ops

An IR op has an op number, an operand *a* and a second operand *f*
(`ir_put(op, a, f)`; IR.md). Its op number is the tag (less `$80` if
bit 7 is set). Its operands come from the tag and from the prefixes
just before it:

- *a* is 0, or the byte after the tag if bit 7 is set, or the word of
  a `W` prefix. The writer uses the byte form for 1 to 255, `W` for
  any other value except 0, and neither for 0. *a* is a 16-bit value,
  and the reader sign-extends a `W` word, so `CONST -1` is `W FF FF`
  then `$02`, and reads back as -1.
- *f* is -1, or the word of an `X` prefix: a function index (`FUNC`,
  `CALL`), a data symbol index (`DATAADDR` and the global ops, for an
  `extern` global), or a label (`JOIN`).

The prefixes come in the order `X`, then `W`, right before their op,
with no other record between. Bit 7 and `W` are never both used for
one op. So the reader needs no table of which ops take which
operands, and neither does the writer: it chooses the form from the
values.

The op numbers are the order of the `IR_*` enum in `src/ir.h`
(changing them changes the version):

| Numbers | Ops |
|---------|-----|
| `$00`-`$04` | `FUNC`, `ENDFUNC`, `CONST`, `DATAADDR`, `STRADDR` |
| `$05`-`$09` | `LOCAL`, `SETLOCAL`, `LOCALB`, `SETLOCALB`, `LOCALADDR` |
| `$0A`-`$0D` | `GLOBAL`, `SETGLOBAL`, `GLOBALB`, `SETGLOBALB` |
| `$0E`-`$15` | `LOAD`, `LOADB`, `STORE`, `STOREB`, `CALL`, `DROP`, `DUP`, `RETURN` |
| `$16`-`$1F` | `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `AND`, `OR`, `XOR`, `SHL`, `SHR` |
| `$20`-`$25` | `EQ`, `NE`, `LT`, `GT`, `LE`, `GE` |
| `$26`-`$28` | `NEG`, `COMP`, `NOT` |
| `$29`-`$2D` | `LABEL`, `JUMP`, `BRFALSE`, `BRTRUE`, `JOIN` |
| `$2E`-`$31` | `SEL`, `CASES`, `CASEVAL`, `CASETO` |
| `$32` | `SWAP` (*a* = top block's length in ops, *f* = the block below's, or -1: don't move code; approach.md 18.7, A) |
| `$33`-`$36` | `ULT`, `UGT`, `ULE`, `UGE` (*a* = the right operand's block length in ops, or 0; 18.7, C) |
| `$37`-`$39` | `UDIV`, `UMOD`, `USHR` (*a* = 1 if the right operand is a constant) |
| `$3A` | `HEAPADDR` (*f* = a data symbol index; version 4) |

The rest, to `$3F`, are free for new ops, and `$60`-`$7F` (with
`$E0`-`$FF`) could be added if those run out.

Examples: `IR_ADD` is `16`. `IR_CONST 0` is `02`. `IR_LOCAL 4` is
`85 04`. `IR_CONST 1000` is `57 E8 03 02` (`W` $03E8, then `CONST`).
`IR_CALL` of function 12 with 2 arguments is `58 0C 00 92 02` (`X`
12, then `CALL` with *a* = 2).

Every op is inside a function, from its `FUNC` to its `ENDFUNC`, and
functions don't nest. `D`, `R`, `Z` and `S` records may come anywhere
between ops, in a function or not (a string literal, a `static`
local), but not between a prefix and its op.

### `L`: a source line

| Size | Field |
|------|-------|
| byte | tag `L` |
| word | line number |

The source line that `ccgen`'s messages give from here on: one comes
right before each function's records (its `N`, prefixes and `FUNC`),
with the line where the function starts. So a back-end error reads
`x.c:120: error 177: function too large: f`.

### `N`: a function's name

| Size | Field |
|------|-------|
| byte | tag `N` |
| word | function index |
| byte | flags: bit 7 set if the function returns a value; the others 0 |
| word | the name's id |

One for each function the IR refers to, before the first op that does
(its `FUNC` or a `CALL`, and before that op's prefixes). `ccgen` keeps
each function's id, and spells it from the name file for its messages
and for `-i`. Bit 7 is for the full build's checks, which need to know
how many values a `CALL` leaves.

### `Y`: the symbol summary

| Size | Field |
|------|-------|
| byte | tag `Y` |
| ... | the rest of the file: `NAME.syms` after its magic and version, with the code size and every function's code offset 0, and each name (a length byte and its characters there) as its id, a word |

`ccgen` writes `NAME.syms` from it: the magic and version, then the
same bytes, with the code size and each defined function's offset
filled in, which only it knows, and each name spelled from the name
file. The name file is read on from the last name looked up, and from
the top only for an earlier one; since ids are given in the order names
are first met, which is nearly the summary's order, all the names take
about one pass over the file.

### What `ccgen` checks

It reports "compiler stream file is damaged" (188) for a wrong magic
or version; an unknown tag; a prefix not followed by an op; an op
outside a function; a label of `MAX_LABELS` or more; a function index
of `MAX_FUNCS` or more (or, at the end, at or beyond the summary's
count), or one used without an `N` record before it; a name id of 0
or beyond the name file's count; a missing summary, or anything after
it; and a function that the summary says is defined but has no `FUNC`,
or the reverse.

The full build (`CCISH_CHECKS`) also checks each block of ops before
an `IR_SWAP` moves it, stashes around it, or an unsigned comparison
flips around it: the block must leave exactly the values its op says
(1, or 2 for a top block ending in `DUP`) and never use one from below
it, following branches and labels (`block_ok()` in `src/irin.c`). Only
a compiler bug (the parser's count of ops disagreeing with the file)
or a damaged file fails it; then the file is reported as damaged,
rather than the wrong code moved. It also rejects a name record with
any flag but bit 7 set. The small build trusts the lengths.

## The final pass

`ccrel` links one or more objects, in the order given, into one
module. It has at most one input file open at a time, and the module.

1. Read every object's `NAME.syms`, laying out each object's data,
   strings and code after the objects before it. Put each symbol
   without flag 8 into one table of shared names (a hash table with
   linear probing, so imports are numbered in its slot order, not the
   order met; approach.md issue 11), where a second
   definition is "multiple definition of" (220), and a declaration
   that disagrees with the most complete one kept so far is
   "conflicting types for" (221): a function's parameter count (unless
   either is 255), flag 4 and type hash (unless either is 0), a
   global's type hash and size (unless either is 0), and a function
   against a global. Then report undefined references, number the
   imports, find `main` (not local), place the zero-filled globals
   (below), and compute the layout: header, dependencies, every
   object's globals, the zero-filled globals that stay, the pointer
   words, every object's string pool, one pad byte, then every
   object's code, then one copy of each
   helper some object holds (the first object's; two copies whose size
   or checksum differ are "helper functions differ between objects",
   229).
2. Write the module header and dependencies. For each object, read its
   `NAME.syms` again to map its symbols to final addresses (through the
   shared table for those it doesn't define), then its stream for its
   `D` and `R` records, writing the data with each `R` record's address
   filled in and kept for the RLD. Then read each stream again for its
   `S` records, writing the string pools, after the zero-filled globals
   that stay and the pointer words (zeros in the file).
3. For each object, map its symbols again, and read its stream a third
   time for its `F` records, copying each function with its relocations
   applied and kept for the RLD. With `-l`, list each function as it
   passes (with several objects, each object's functions after an
   `object NAME:` line). Then copy each helper from the stream of the
   object it was taken from (listed as `helper NAME:`), then add the
   init function, which sets up the moved globals (below) and calls
   `main`.
4. Write the RLD (function entries in code order, the helpers'
   included, then the kept
   relocations, those in the data first) and the ESD (imports). With
   `-l`, end the listing with how full the shared-name table is:
   `; shared names: N of M slots, B of C name bytes`. (The listing
   also begins with a `; heap global N: NAME, SIZE bytes` line for
   each global moved to the heap, numbered as their pointer words;
   `tools/loops.py` reads them.)

A kind 1 or 3 target is offset by where its object's data or strings
start; a kind 4 relocation becomes a kind 1 relocation to the symbol's
final data offset plus the addend; a kind 5 is the helper's address in
the module, relocated like a function's. The final pass checks that the
files agree with each other (sizes, offsets, indexes, record tags), and
reports "compiler stream file is damaged" if not.

**Zero-filled globals** (approach.md, "Large zero-filled data on the
heap"). Each one (flag 64) whose address no object's initializer holds (flag 128 in no
entry for it) moves to the heap: `ccrel` gives it a **pointer word**,
after the data, and turns each code reference to it, an `LA` of its
address, into an `LAW` of its pointer word. A program's moved globals
are one block, which the init function takes from `heapalloc` and
zeroes with `memset` before it calls `main`, then points each pointer
word at its global. It first checks, with `heapavail` and `isult`, that
the heap holds the block and 512 bytes more for `main`'s frames; if not,
it prints `not enough memory` and returns -1, so that the loader
reports an error and `main` never runs. (`heapavail` comes first
because plvm's `heapalloc` stops the VM when the heap is full, and the
Apple's can wrap past `$FFFF`.) A zero-filled global that doesn't move
gets zero bytes after every object's data. So the module file holds
neither: it is smaller, and so is the memory the loader needs to read
it in. At most 32 globals move ("too many large globals to move to the
heap", 308), of at most 65,023 bytes in all ("too much data to move to
the heap", 309). The `LAW` costs about 24 cycles more than the `LA`
did, and a moved global's element at a constant index, or a struct
member, takes an `LAW`, an `ADDI` or `CW` and `ADD`, and a load or
store, where an `LAB` or `SAW` would have done.

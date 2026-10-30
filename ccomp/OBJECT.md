# ccish stream files (object format)

The compiler proper (stages 1–4) doesn't write the REL module itself.
It writes a **stream file** and a **symbol summary**, and a separate
**final pass** (`src/link.c`, the program `ccrel`) turns them into the
module. See [approach.md](approach.md), 18.1, for why. Together the
two files are ccish's object file format, an **object**. The final
pass is also a linker, which links several objects into one module
(issue 11), so this format should change only deliberately, with the
version number bumped.

For output `NAME`, the files are `NAME.obj` and `NAME.syms`. `ccish`
removes them after the module is written, and after any error, unless
`-k` is given; `cccomp` leaves them for `ccrel`, which keeps them. This is format version 4, which adds what linking
several files needs (approach.md issue 11): data symbols, local
(`static`) symbols, a relocation to an `extern` global, and type
hashes. Version 3 had none of these. M5's version 2 had four stream
files (`.data`, `.drel`, `.strs` and `.code`), which version 3 made
records in the one stream file, so that the compile pass has only the
source and one output file open (PLASMA's Apple II file library allows
4 open files; approach.md 18.6 B6a).

All words are 16-bit little-endian. Every file is written once, in
order, with no seeking, through 512-byte block buffers (`src/bio.c`).

## `NAME.obj`: the stream

A sequence of records, until end of file, in the order the compiler
made them: data, data relocations and strings as the source declares
them, and each function when it ends, so the kinds are interleaved.
Each record starts with a one-byte tag, a letter:

| Tag | Record |
|-----|--------|
| `D` | a run of data bytes |
| `R` | a data word that holds an address |
| `S` | a string literal |
| `F` | a function |

The `D` and `R` records, in order, make up the data (the global
variables); the `S` records, in order, make up the string pool; and
the `F` records, in order, make up the code. The final pass reads the
stream three times, once for each, skipping the other records, and a
fourth time for the `R` records and the `F` records' relocations,
which give the RLD's relocations. The RLD's function entries come from
the `.syms` files (each defined function's offset).

### `D`: data bytes

| Size | Field |
|------|-------|
| byte | tag `D` |
| byte | count *n* (1–128) |
| *n* bytes | the next bytes of the data |

The global variables' bytes, in the order they appear in the source:
each global's initial value, or zero bytes of its size if it has none.
A long run is split into several records.

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
declared `extern` and not (yet) defined in this file, plus the addend
(`int *q = &ext[3];`).

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
| byte | kind: 1 = data address, 2 = function address, 3 = string address, 4 = data symbol address |
| word | offset of the word, from the start of this function |
| word | target: a data offset (kind 1), a function symbol index (kind 2), a string pool offset (kind 3), or a data symbol index (kind 4) |

The word itself is written as 0, except for kind 4, where it holds the
addend; the final pass fills it in. A function address may refer to a
function defined later in the file, or to an import, which the loader
fills in. A data symbol address (kind 4) refers to a global declared
`extern`, used before any definition in this file: a reference to a
global with storage here is kind 1.

The function's code offset in the code segment is the sum of the
lengths of the `F` records before it. The frame sizes in `ENTER` and
`LEAVE` are already filled in, and so are branch offsets: branches
stay within a function, are relative, and are resolved in the
compiler's function buffer, so they need no records here.

## `NAME.syms`: the symbol summary

Written last, when compilation ends. The final pass reads it first, so
that every address is known before anything is written. It is a file
of its own so that the compiler needn't seek back to patch a header.

| Size | Field |
|------|-------|
| 2 bytes | magic `CI` |
| byte | format version: 4 |
| word | data size |
| word | string pool size |
| word | code size (the total code length of the `F` records) |
| word | function symbol count *N* |
| word | data symbol count *M* |
| *N* entries | function symbols, in index order |
| *M* entries | data symbols, in index order |

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
| byte | flags: 1 = defined here, 2 = used, 8 = local (`static`) |
| word | data offset, if defined; otherwise 0 |
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
   imports, find `main` (not local), and compute the layout: header,
   dependencies, every object's globals, every object's string pool,
   one pad byte, then every object's code.
2. Write the module header and dependencies. For each object, read its
   `NAME.syms` again to map its symbols to final addresses (through the
   shared table for those it doesn't define), then its stream for its
   `D` and `R` records, writing the data with each `R` record's address
   filled in and kept for the RLD. Then read each stream again for its
   `S` records, writing the string pools.
3. For each object, map its symbols again, and read its stream a third
   time for its `F` records, copying each function with its relocations
   applied and kept for the RLD. With `-l`, list each function as it
   passes (with several objects, each object's functions after an
   `object NAME:` line). Then add the init function, which calls
   `main`.
4. Write the RLD (function entries in code order, then the kept
   relocations, those in the data first) and the ESD (imports). With
   `-l`, end the listing with how full the shared-name table is:
   `; shared names: N of M slots, B of C name bytes`.

A kind 1 or 3 target is offset by where its object's data or strings
start; a kind 4 relocation becomes a kind 1 relocation to the symbol's
final data offset plus the addend. The final pass checks that the
files agree with each other (sizes, offsets, indexes, record tags), and
reports "compiler stream file is damaged" if not.

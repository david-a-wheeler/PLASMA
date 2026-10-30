# ccish stream files (object format)

The compiler proper (stages 1–4) doesn't write the REL module itself.
It writes a **stream file** and a **symbol summary**, and a separate
**final pass** (`src/link.c`, the program `ccrel`) turns them into the
module. See [approach.md](approach.md), 18.1, for why. Together the
two files are ccish's object file format. The final pass is meant to
become a linker (issue 11), so this format should change only
deliberately, with the version number bumped.

For output `NAME`, the files are `NAME.obj` and `NAME.syms`. They are
removed after the module is written, and after any error, unless
`ccish -k` is given. This is format version 3: M5's version 2 had four
stream files (`.data`, `.drel`, `.strs` and `.code`), which are now
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
stream three times, once for each, skipping the other records.

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
| byte | kind: 1 = data address, 3 = string address |
| word | target: a data offset (kind 1), or a string pool offset (kind 3) |

It stands for the next two bytes of the data (`char *p = "hi";`,
`int *q = &x;`), so its offset is where it falls among the `D` and `R`
records. The final pass writes the address there, and adds an RLD entry
so that the loader can adjust it.

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
| byte | kind: 1 = data address, 2 = function address, 3 = string address |
| word | offset of the word, from the start of this function |
| word | target: a data offset (kind 1), a symbol index (kind 2), or a string pool offset (kind 3) |

The word itself is written as 0; the final pass fills it in. A function
address may refer to a function defined later in the file, or to an
import, which the loader fills in.

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
| byte | format version: 3 |
| word | data size |
| word | string pool size |
| word | code size (the total code length of the `F` records) |
| word | symbol count *N* |
| *N* entries | symbols, in index order |

Each symbol entry:

| Size | Field |
|------|-------|
| byte | flags: 1 = defined here, 2 = called, 4 = returns a value |
| byte | parameter count, or 255 if unknown |
| word | code offset, if defined; otherwise 0 |
| byte | name length *n* (1–31) |
| *n* bytes | name |

A symbol that is called but not defined is an import. Its name must be
a CMDSYS function, which the compiler has already checked. Exported
names are the defined ones. The parameter count and value flag aren't
needed to write one module, but a linker will need them to check calls
between files.

## The final pass

It has at most one input file open at a time, and the module.

1. Read `NAME.syms`. Number the imports, find `main`, and compute the
   layout: header, dependencies, globals, string pool, one pad byte,
   then code.
2. Write the module header and dependencies. Read the stream for its
   `D` and `R` records, writing the data with each `R` record's address
   filled in and kept for the RLD; then read it again for its `S`
   records, writing the string pool.
3. Read the stream a third time for its `F` records, copying each
   function with its relocations applied and kept for the RLD. With
   `-l`, list each function as it passes. Then add the init function,
   which calls `main`.
4. Write the RLD (function entries in code order, then the kept
   relocations, those in the data first) and the ESD (imports).

It checks that the files agree with each other (sizes, offsets,
indexes, record tags), and reports "compiler stream file is damaged"
if not.

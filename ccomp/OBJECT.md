# ccish stream files (object format)

The compiler proper (stages 1–4) doesn't write the REL module itself.
It writes three **stream files**, and a separate **final pass**
(`src/link.c`) turns them into the module. See
[approach.md](approach.md), 18.1, for why. Together the three files
are ccish's object file format. The final pass is meant to become a
linker (issue 11), so this format should change only deliberately,
with the version number bumped.

For output `NAME`, the files are `NAME.data`, `NAME.code` and
`NAME.syms`. They are removed after the module is written, and after
any error, unless `ccish -k` is given.

All words are 16-bit little-endian. Every file is written and read
once, in order, with no seeking, through 512-byte block buffers
(`src/bio.c`).

## `NAME.data`: the data segment

The data segment's bytes, in order, with no header or tags. Its length
is the data size. Data offsets elsewhere count from its first byte.

So far it holds only string literals, each as `[length][chars][NUL]`
(issue 8). Initialized globals will follow.

## `NAME.code`: functions and their relocations

A sequence of function records, in code order, until end of file:

| Size | Field |
|------|-------|
| word | symbol index of the function (into `NAME.syms`) |
| word | code length *L* |
| *L* bytes | the function's bytecode |
| word | relocation count *R* |
| *R* × 5 bytes | relocation records |

Each relocation record marks a word in this function's bytecode that
needs a final address:

| Size | Field |
|------|-------|
| byte | kind: 1 = data address, 2 = function address |
| word | offset of the word, from the start of this function |
| word | target: a data offset (kind 1), or a symbol index (kind 2) |

The word itself is written as 0; the final pass fills it in. A function
address may refer to a function defined later in the file, or to an
import, which the loader fills in.

The function's code offset in the code segment is the sum of the
lengths of the records before it. The frame sizes in `ENTER` and
`LEAVE` are already filled in. (From M3 step 3, branches within a
function are resolved in the compiler's function buffer, so they need
no records here.)

## `NAME.syms`: the symbol summary

Written last, when compilation ends. The final pass reads it first, so
that every address is known before anything is written.

| Size | Field |
|------|-------|
| 2 bytes | magic `CI` |
| byte | format version: 1 |
| word | data size (the length of `NAME.data`) |
| word | code size (the total code length in `NAME.code`) |
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

1. Read `NAME.syms`. Number the imports, find `main`, and compute the
   layout: header, dependencies, data, one pad byte, then code.
2. Write the module header and dependencies, then copy `NAME.data`.
3. Copy `NAME.code` one function at a time, applying its relocations
   and keeping them for the RLD. With `-l`, list each function as it
   passes. Then add the init function, which calls `main`.
4. Write the RLD (function entries in code order, then the kept
   relocations) and the ESD (imports).

It checks that the files agree with each other (sizes, offsets,
indexes), and reports "compiler stream file is damaged" if not.

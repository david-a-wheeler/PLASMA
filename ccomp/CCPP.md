# ccpp and its output: the token stream and the name file

`ccpp` is ccish's preprocessor and lexer (approach.md decision 24,
and "`ccpp` design" under Milestones). It reads a C source file and
writes two files for `cccomp`, the front end:

- **`NAME.i`, the token stream** (binary): the source as tokens, each
  keyword and operator one byte, numbers converted, strings decoded
  and joined, and a record for each line, so that messages give the
  right line.
- **`NAME.nam`, the name file** (text): the spelling of each
  identifier, one per line, then the source files' names. In the
  token stream, an identifier is only its number, its *id*.

The two files are made together and only make sense together: an id
in `NAME.i` is a line of `NAME.nam`, and each carries the same
*stamp* (below), so that a reader can tell that they belong together.

This document describes both. [OBJECT.md](OBJECT.md) describes the
files after them (`NAME.ir`, `NAME.obj` and `NAME.syms`).

## The pipeline

```
NAME.c --ccpp--> NAME.i + NAME.nam --cccomp--> NAME.ir --ccgen--> NAME.obj + NAME.syms --ccrel--> module
```

- `ccpp [-k] [-o output] input.c` writes `output.i` and
  `output.nam` (the output name is the input's without `.c`, unless
  `-o`). After an error it removes both, unless `-k`.
- `cccomp [-k] [-o output] input` reads `input.i` and `input.nam`,
  and writes `output.ir` (the output name is `input` unless `-o`),
  whose names are ids from `input.nam`. After it succeeds it removes
  `input.i`, unless `-k`; after an error it removes `output.ir`
  (unless `-k`) and keeps its inputs, as `ccgen` keeps the IR file.
- `ccgen` reads `NAME.ir` and `NAME.nam`, to spell the names it
  writes into `NAME.syms` and its messages, and removes both after it
  succeeds, unless `-k` (OBJECT.md).
- `ccish`, for the host, runs every stage through the same files and
  removes them all, unless `-k`.

`cccomp` never needs an identifier's spelling: it reads one only to
print a message. `ccgen` is the first to need spellings, since the
objects it writes are linked by name; it checks the name file, and its
stamp against the IR file's, before it reads a name. A struct's type
hash, which includes its tag's spelling so that it agrees between
files, comes from the hash `ccpp` writes with each tag (`$09`): adding
the *n* bytes one at a time to a hash *h* gives *h* × 31^*n* + the
spelling's own hash.

**Open files.** `ccpp` has the source, the include files being read,
and `NAME.i` open; it writes `NAME.nam` last, after closing the
others. `cccomp` has `NAME.i` and `NAME.ir` open, and opens
`NAME.nam` to look a name up for a message; `ccgen` has `NAME.ir`,
its output and `NAME.nam` open. PLASMA's Apple II file library allows
4 open files.

## `NAME.i`: the token stream

All words are 16-bit little-endian. The file is written once, in
order, through a 512-byte block buffer (`src/bio.c`).

**The header**: the 3 bytes `CTK`, then the format version, a byte:
**1**. The source file's name is not here: it is file 1 in
`NAME.nam`.

**Then the tokens**, each a code byte, followed by its operands:

| Code | Token | Operands |
|---|---|---|
| `$00` | the end of the input | the stamp: the id count (word), the checksum (word); nothing may follow |
| `$01` | the next line | none |
| `$02` | a jump to a line (reserved: written from step 4, after a skipped `#if` block) | the line (word) |
| `$03` | a change of file (reserved: written from step 4, on entering or leaving an `#include`) | the file number (byte), the line (word) |
| `$04` | an identifier | its id (word) |
| `$05` | a number, `int` | its value (word) |
| `$06` | a number, `unsigned` (a `u` suffix, or a hex or octal constant over `$7FFF`) | its value (word) |
| `$07` | a string literal | its length (byte, 0-255), then its bytes |
| `$08` | a C keyword ccish doesn't support (`float`, `union`, ...) | its id (word), for the message's spelling |
| `$09` | a struct tag: an identifier right after `struct` (no other identifier is written so) | its id (word); the hash *h* of its spelling, from 0, for each byte *b*: *h* = (*h* × 31 + *b*) mod 65536 (word); and 31^*n* mod 65536, for its *n* bytes (word) |
| `$20`-`$7E` | a one-character token, as itself: one of `! % & ( ) * + , - . / : ; < = > ? [ ] ^ { \| } ~` | none |
| `$80`-`$BF` | a supported keyword (table below) | none |
| `$C0`-`$DF` | an operator of more than one character (table below) | none |

Any other code is a damaged file.

**The keywords**, `$80` up, in this order (the order of `T_INT` to
`T_UNSIGNED` in `cccomp`, and of `keywords[]` in `src/pplex.c`):

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| `$80` | `int` | `void` | `return` | `if` | `else` | `while` | `do` | `for` |
| `$88` | `break` | `continue` | `char` | `sizeof` | `static` | `const` | `volatile` | `struct` |
| `$90` | `switch` | `case` | `default` | `typedef` | `enum` | `extern` | `signed` | `unsigned` |

**The operators**, `$C0` up (the order of `T_EQ` to `T_ELLIPSIS`):

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| `$C0` | `==` | `!=` | `<=` | `>=` | `<<` | `>>` | `&&` | `\|\|` |
| `$C8` | `++` | `--` | `+=` | `-=` | `*=` | `/=` | `%=` | `&=` |
| `$D0` | `\|=` | `^=` | `<<=` | `>>=` | `->` | `...` | | |

Keywords and operators have separate ranges so that adding a keyword
(`union`, `long`) doesn't renumber the operators. Adding or
reordering either changes the format version.

**Digraphs** (`<: :> <% %>`) are written as the tokens they stand for
(`[ ] { }`). A character constant is a number (`$05`): its value is
the character's code.

**Strings.** Escapes are decoded, and adjacent string literals are
joined (C's translation phase 6), so `cccomp` sees one string. A
string has at most 255 bytes (`MAX_STR`), so that every literal is
also a PLASMA string; a longer one is message 12. It may contain any
byte, including 0.

**Lines.** `ccpp` writes a `$01` for each newline it passes, in order
with the tokens: newlines in comments and between joined strings
too. So the line of a token is 1 plus the number of `$01`s before it.
Joined strings are written once the next token is known, after the
`$01`s between them, so a string's line is the line of the token after
it, as it was when `cccomp` lexed the source itself. (`$02` and `$03`
set the line, and the file, outright.)

**Lines for messages.** `cccomp` reports an error at the current
token's line, and `next_is_rbracket()` asks whether the next code is
`]` (a `$01` between them means no).

## `NAME.nam`: the name file

Text, one entry per line, each ending with a newline (LF). Text so
that a person can read it with any editor; it is no larger than a
binary form, since a newline takes the place of a length byte. An
identifier can't contain a newline, and nor can a ProDOS path, so
nothing is escaped.

```
putc            id 1: the fixed ids, 1-27, in every file
putln           id 2
...
isule           id 25
main            id 26
pstr            id 27
count           id 28: then every other name, in the order ccpp met it
...
@t.c            file 1: the source
@stdio.h        file 2, and so on, in the order first included
                a blank line
=1 001C 3F7C    the stamp: the format version, the id count, the checksum
```

**Names.** Line *n* is the spelling of id *n*, from 1 (id 0 is "no
name"). Ids are given in the order `ccpp` first meets each name: every
identifier, including macro names, macro parameters and names in `#if`
lines, whether or not it reaches the token stream, so that `NAME.nam`
is simply `ccpp`'s name pool written out (no table from ids to names
is needed). C's keywords are not names: they never get an id, except
the unsupported ones (`$08`), whose spelling the message needs. A
name has at most 31 characters (`MAX_NAME`); `ccpp` keeps only the
first 31, so readers may rely on that.

**The fixed ids** come first, in every file, in this order: the 25
CMDSYS functions `cccomp` knows (`putc`, `putln`, `puts`, `puti`,
`putb`, `puth`, `getc`, `gets`, `call`, `syscall`, `heapmark`,
`heapallocalign`, `heapalloc`, `heaprelease`, `heapavail`, `memset`,
`memcpy`, `strcpy`, `strcat`, `toupper`, `sext`, `isugt`, `isuge`,
`isult`, `isule`: ids 1-25), then `main` (26) and `pstr` (27): every
name C-ish gives a meaning of its own. So `cccomp` knows them by number
(`src/fixed.h`), with no spellings.

**Files.** After the names, one line for each source file, starting
with `@`: file *k* is the *k*-th of them, and file 1 is the source
itself, as named to `ccpp`. A file number in the token stream (`$03`)
is one of these, and messages look a file's name up as they look up an
identifier's.

**The blank line** comes after the last file. It is written, and
checked when the file is read, so that an off-by-one or other damage
(a lost or extra line, the stamp read as a name) is found rather than
quietly giving the wrong names.

**The stamp** is the last line: `=`, the format version (**1**, in
decimal), a space, the id count (4 hex digits, upper case), a space,
and the checksum (4 hex digits). Nothing follows its newline.

**The checksum** is over every byte of the file before the blank
line (the names, the files, and their newlines): starting from 0, for
each byte *b*, *h* = (*h* × 31 + *b*) mod 65536 (the same sum as the
helper functions', OBJECT.md).

## The stamp: how the two files are matched

`ccpp` writes the same id count and checksum at the end of both files:
in `NAME.i` after its `$00`, as two words, and in `NAME.nam` as its
last line. A reader of both compares them, so that a `NAME.nam` from
another run of `ccpp` (say, after the source changed and only `ccpp`
was run again) is never trusted with the ids in `NAME.i`.

**What `cccomp` checks** (and `ccgen`, of the name file, against the
IR file's stamp):
- `NAME.i`: its header and version (else message 230, "not a token
  file from ccpp, or the wrong version"); that each code is known,
  that the file doesn't end before `$00`, and that nothing follows its
  stamp (else 231, "token file is damaged").
- `NAME.nam`, when it opens the token stream, before using any name:
  that its names come before its files, none longer than 31
  characters; that at least one file follows them, then the blank
  line, then a stamp of the right form and version whose count and
  checksum match the file itself, and then nothing; and, for each id
  used, that there is such a name. At the end of the token stream:
  that its stamp is the token stream's, so that a well-formed name
  file from another run of `ccpp` is refused too. Any of these failing
  is message 232, "name file is stale or damaged".

## An example

For `t.c` holding the one line `int main() { return 0; }`, `ccpp`
writes `t.i`:

```
43 54 4B 01    "CTK", version 1
80             int
04 1A 00       an identifier: id 26, main
28 29 7B       ( ) {
82             return
05 00 00       the number 0
3B 7D          ; }
01             the next line
00 1B 00 ss ss the end: 27 ids, and the checksum
```

and `t.nam`: the 27 fixed names (`main` is one of them, so no other
name is added), then `@t.c`, a blank line, and `=1 001B ssss`, where
`ssss` is the checksum of the 28 lines before the blank line (`ss ss`
in `t.i`, low byte first).

## Limits

| Limit | Value | Message |
|---|---|---|
| A line (after joining lines that end in `\`, from step 4) | 255 characters | 233 |
| A string literal, after joining | 255 bytes (`MAX_STR`) | 12 |
| An identifier | 31 significant characters (`MAX_NAME`); the rest are ignored | none |
| `ccpp`'s name pool: every name and file name, a length byte each | `MAX_PP_POOL` bytes | 18 |

## Versions

- **`NAME.i` version 1, `NAME.nam` version 1** (step 1 of the `ccpp`
  design): the first; step 2 began writing the `$09` tags, reserved
  from the start. Directives (steps 4-6) will add the `$02` and `$03`
  records, reserved now too, so the versions shouldn't need to change
  for them.

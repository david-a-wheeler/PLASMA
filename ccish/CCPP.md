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

- `ccpp [-k] [-o output] [-I dir] [-D name[=text]] [-U name] input.c`
  writes `output.i` and `output.nam` (the output name is the input's
  without `.c`, unless `-o`). After an error it removes both, unless
  `-k`. `-I` adds a directory to look for `#include` files in (also
  written `-Idir`); `-D` defines a macro as `text`, or as 1, and `-U`
  undefines one, in the order given, before the source (also written
  `-Dname`, `-Uname`). `ccish` takes them too.
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
print a message, so it doesn't open the name file at all unless it
prints one. It copies the stamp from the end of the token stream to
the end of the IR file. `ccgen` is the first to need spellings, since
the objects it writes are linked by name; it checks the name file when
it starts, and its stamp against the IR file's before it spells a name
into `NAME.syms`. A struct's type
hash, which includes its tag's spelling so that it agrees between
files, comes from the hash `ccpp` writes with each tag (`$09`): adding
the *n* bytes one at a time to a hash *h* gives *h* × 31^*n* + the
spelling's own hash.

**Open files.** `ccpp` has the source, the include files being read,
and `NAME.i` open; it writes `NAME.nam` last, after closing the
others. `cccomp` has `NAME.i` and `NAME.ir` open, and opens
`NAME.nam` only to look a name up for a message: in the small build,
after closing the others (src/build.h, `CCISH_DIAG`); `ccgen` has `NAME.ir`,
its output and `NAME.nam` open. PLASMA's Apple II file library allows
4 open files.

## Preprocessing

`ccpp` carries out these directives (approach.md, "`ccpp` design",
steps 4-6):

- **`#include "file"`** looks for `file` beside the file that
  includes it, then in each include directory in order; **`#include
  <file>`** only in the include directories. Those are the `-I` ones,
  then the C library's headers: `CCISH_INC` if the build defines it
  (the Makefile does, as `lib/include`'s absolute path), else, for a
  ccpp built by ccish, `ccinc` in CMDSYS's `syspath` (`sys/ccinc`
  under plvm). The default counts as one of the 8 `MAX_INC_DIRS`.
  Includes nest up to
  `MAX_INCLUDE` (3) deep beside the source, or fewer if the system
  can't open that many files (message 236 says the file is missing or
  too many files are open). Each file read gets a number in
  `NAME.nam`.
- **`#define name tokens`** defines an object-like macro, and
  **`#define name(params) tokens`** a function-like one (the `(` right
  after the name, with no blank): the parameters are names, the last
  of which may be `...`, which the body uses as `__VA_ARGS__`. A macro
  may be defined again only with the same parameters and tokens.
  **`#undef name`** undefines one. A macro's name is an identifier, or
  one of the C keywords ccish doesn't support (`long`, say), which are
  names here too; a keyword ccish supports can't be one. `__CCISH__` is
  defined as 1.
- A function-like macro's name followed by `(` is a call (the `(` may
  be on a later line); otherwise the name stays as it is. Each argument
  is fully macro-expanded by itself before it is substituted (C11
  6.10.3.1), so `MAX(MAX(a, b), c)` works, except next to `#` or `##`,
  where it is used as written. `#param` makes a string of the
  argument's tokens, spelled again, with one space where there were
  blanks between them. `a ## b` joins two tokens into one, which must
  be a name, a decimal number or an operator. The replacement is read
  again for more macros, with what follows it, but a macro isn't
  expanded again inside its own expansion (C11 6.10.3.4).
- **`#if`**, **`#ifdef`**, **`#ifndef`**, **`#elif`**, **`#else`** and
  **`#endif`** keep or skip groups of lines; a skipped group's lines
  aren't split into tokens, so they may hold anything (an apostrophe,
  say), but its conditional directives still count, to find its end.
  Every group must end in the file it began in. `#if` and `#elif`
  take an integer expression, with its macros expanded and
  `defined(name)` or `defined name` 1 if `name` is a macro, else 0;
  any name left is 0. It has C's operators but the assignments, `++`,
  `--` and the comma, and `&&`, `||` and `?:` don't evaluate what they
  skip (`0 && 1 / 0` is no error). An `#elif` after a branch was kept
  isn't evaluated at all.
- **`#error text`** stops, with the text (message 258).
- **`#line number`** makes the next line that number, and **`#line
  number "file"`** also names the file so from here on (in messages,
  `__FILE__` and the line records).
- **`__LINE__`** is the current line's number, and **`__FILE__`** the
  current file's name, as a string.
- **`#pragma plasma import NAME`** names a PLASMA module to load for
  the externs declared after it in the same file (approach.md issue 22,
  decision 35). It becomes two tokens, the fixed id `__plasma_import`
  and `NAME`, for `cccomp`. Any other **`#pragma`** is ignored (its
  first word is read as text, and gets no id). A **`#`** alone does
  nothing. Any other directive is an error.
- **An asm label**, `__asm__("PERR")`: the string after the fixed id
  `__asm__` and `(` is written as an identifier (`PERR`, given an id
  then), not a string, so that `cccomp`, which keeps no strings, can
  pass the PLASMA name along by id.
- A line that ends in a backslash is joined to the next (C's phase 2).

Known differences from C, so far:
- `#if` arithmetic is ccish's own: 16-bit `int` and `unsigned`, with
  C's usual conversions (C says the widest types, `intmax_t` and
  `uintmax_t`), so `0x7FFF + 1 < 0` is true here.
- `#` spells a number in decimal (`0x10` as `16`, a character
  constant `'A'` as `65`), a string with `\n`, `\t`, `\r`, `\"` and
  `\\` escaped and any other unprintable byte in octal, and puts a space
  only where the source had blanks, not between tokens that came from
  another macro's body.
- `##` makes only names, decimal numbers (with a `u`) and operators.
- A name a macro's own expansion left unexpanded is expanded if it is
  read again later (C would leave it unexpanded for good).
- A comment that begins on a directive's line and continues past it
  ends the directive there (C treats the comment as a space, so the
  directive would go on).
- Extra tokens after `#else`, `#endif`, `#undef` or a file's name are
  an error, also in a skipped group (C allows them there).

### `-E`: the tokens as text

`ccpp -E` (in the full build only, for now: `CCISH_TEXT`) writes the
tokens as C source to the standard output, as `gcc -E` does, instead
of `NAME.i` and `NAME.nam`: a space between tokens, each on its own
line (newlines for up to 8 lines), and a line marker, `# line
"file"`, for a change of file or a longer jump. Tokens are spelled as
`#` spells them (numbers in decimal), and adjacent strings are already
joined. All of it is in `src/ppout.c`'s `CCISH_TEXT` parts, so that the
small build can have it too by defining `CCISH_TEXT` there; it costs
about 1.4K.

## `NAME.i`: the token stream

All words are 16-bit little-endian. The file is written once, in
order, through a 512-byte block buffer (`src/bio.c`).

**The header**: the 3 bytes `CTK`, then the format version, a byte:
**8**. The source file's name is not here: it is file 1 in
`NAME.nam`.

**Then the tokens**, each a code byte, followed by its operands:

| Code | Token | Operands |
|---|---|---|
| `$00` | the end of the input | the stamp: the id count (word), the checksum (word); nothing may follow |
| `$01` | the next line | none |
| `$02` | a jump to a line, over more than 3 lines (below) | the line (word) |
| `$03` | a change of file: into an `#include`d file, or back | the file number (byte), the line (word) |
| `$04` | an identifier | its id (word) |
| `$05` | a number, `int` | its value (word) |
| `$06` | a number, `unsigned` (a `u` suffix, or a hex or octal constant over `$7FFF`) | its value (word) |
| `$07` | a C keyword ccish doesn't support (`float`, `long`, ...) | its id (word), for the message's spelling |
| `$08` | a string literal | its length (byte, 0-255), then its bytes |
| `$09` | a struct or union tag: an identifier right after `struct` or `union` (no other identifier is written so) | its id (word); the hash *h* of its spelling, from 0, for each byte *b*: *h* = (*h* × 31 + *b*) mod 65536 (word); and 31^*n* mod 65536, for its *n* bytes (word) |
| `$10`-`$1F` | never written: ccpp's own codes, for macros' bodies (`src/pplex.h`) | |
| `$20`-`$7E` | a one-character token, as itself: one of `! % & ( ) * + , - . / : ; < = > ? [ ] ^ { \| } ~` | none |
| `$80`-`$BF` | a supported keyword (table below) | none |
| `$C0`-`$DF` | an operator of more than one character (table below) | none |

Any other code is a damaged file.

**The keywords**, `$80` up, in this order (the order of `TC_INT` to
`TC_STATIC_ASSERT` in `src/token.h`, and of `keywords[]` in
`src/pplex.c`). `cccomp`'s parser uses these codes as its tokens, so
the set tests on them are in `src/token.h` too. Each set the parser tests is a block whose size is a
power of 2, starting at a multiple of its size, so a test is one AND
and one compare; a block's spare codes (blank here, UNUSED in the
sources) are room for a new keyword of its kind, and are never
written. `$80`-`$8F` are the type-name keywords: the int-type
specifiers and the qualifiers, a block of 8; the tagged types, struct
and union first; `void`. Then the storage classes, `typedef` and
`constexpr`, and the switch labels; then the rest:

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| `$80` | `int` | `char` | `signed` | `unsigned` | `const` | `volatile` | | |
| `$88` | `struct` | `union` | `enum` | | `void` | | | |
| `$90` | `static` | `extern` | `register` | | `typedef` | `constexpr` | `case` | `default` |
| `$98` | `return` | `if` | `else` | `while` | `do` | `for` | `break` | `continue` |
| `$A0` | `sizeof` | `switch` | `goto` | `_Static_assert` | | | | |

**The operators**, `$C0` up (the order of `TC_ADDEQ` to `TC_ELLIPSIS`),
in blocks as the keywords are: first the compound assignments, in the
order of the binary IR ops they do (with the same spare places), then
`++` and `--`, `&&` and `||`, each a pair, then the rest:

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| `$C0` | `+=` | `-=` | `*=` | `&=` | `\|=` | `^=` | | |
| `$C8` | `/=` | `%=` | `<<=` | `>>=` | | | | |
| `$D0` | `++` | `--` | `&&` | `\|\|` | `==` | `!=` | `<=` | `>=` |
| `$D8` | `<<` | `>>` | `->` | `...` | | | | |

Keywords and operators have separate ranges so that adding a keyword
(`long`) doesn't renumber the operators. Adding or reordering either
changes the format version.

`short` is a synonym for `int` (ccish has one word type, and the
standard C distinction between the two is not worth implementing): the
preprocessor writes it as the `int` token ($80), so no short token of
its own exists, and the compiler never sees the word.

**Digraphs** (`<: :> <% %>`) are written as the tokens they stand for
(`[ ] { }`). A character constant is a number (`$05`): its value is
the character's code.

**Strings.** Escapes are decoded, and adjacent string literals are
joined (C's translation phase 6), so `cccomp` sees one string. A
string has at most 255 bytes (`MAX_STR`), so that every literal is
also a PLASMA string; a longer one is message 12. It may contain any
byte, including 0.

**Lines.** A reader starts at line 1 of file 1. Before each token,
`ccpp` brings the reader's file and line up to the token's: a `$01`
for each line, for up to 3 lines; a `$02` and the line for more (after
a skipped group, or many blank lines); and a `$03`, with the file's
number and the line, when the file changes (into an `#include`d file,
or back). So directives, blank lines and skipped groups cost nothing,
or 3 bytes. The end code is brought up to date too, so the end's line
is the last line plus one when the file ends with a newline. Joined
strings are written once the next token is known, after its line is
brought up to date, so a string's line is the line of the token after
it, as it was when `cccomp` lexed the source itself. A line joined to
the one before it by a backslash counts as that one; the lines after
it are numbered as in the file.

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
__putc          id 1: the fixed ids, 1-33, in every file
__putln         id 2
...
__isule         id 25
main            id 26
pstr            id 27
...
defined         id 31
__asm__         id 32
__plasma_import id 33
count           id 34: then every other name, in the order ccpp met it
...
@t.c            file 1: the source
@stdio.h        file 2, and so on, in the order first included
                a blank line
=2 0022 3F7C    the stamp: the format version, the id count, the checksum
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
CMDSYS functions `cccomp` knows, each its PLASMA name with `__` in
front (`__putc`, `__putln`, `__puts`, `__puti`, `__putb`, `__puth`,
`__getc`, `__gets`, `__call`, `__syscall`, `__heapmark`,
`__heapallocalign`, `__heapalloc`, `__heaprelease`, `__heapavail`,
`__memset`, `__memcpy`, `__strcpy`, `__strcat`, `__toupper`, `__sext`,
`__isugt`, `__isuge`, `__isult`, `__isule`: ids 1-25; approach.md
decision 35), then `main` (26) and `pstr` (27): every
name C-ish gives a meaning of its own; then `__CCISH__` (28),
`__FILE__` (29), `__LINE__` (30) and `defined` (31), which `ccpp`
gives one; then `__asm__` (32) and `__plasma_import` (33), for imports
from other modules. So `cccomp` and `ccpp` know them by number
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

**The stamp** is the last line: `=`, the format version (**2**, in
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
IR file's stamp, which `cccomp` copies from the token stream's):
- `NAME.i`: its header and version (else message 230, "not a token
  file from ccpp, or the wrong version"); that each code is known
  (a one-character token's only as printable, `$21`-`$7E`: one
  `ccpp` never writes, such as `#`, is an unexpected token to the
  parser, an error all the same), that the file doesn't end before
  `$00`, and that nothing follows its stamp (else 231, "token file is
  damaged").
- `NAME.nam`, only if it opens it, for a message, before using any
  name (`ccgen` always, when it starts): that its names come before
  its files, none longer than 31 characters; that at least one file
  follows them, then the blank line, then a stamp of the right form
  and version whose count and checksum match the file itself, and
  then nothing; and, for each id used, that there is such a name.
  `ccgen` also checks that its stamp is the IR file's (which `cccomp`
  copied from the token stream), so that a well-formed name file from
  another run of `ccpp` is refused too. Any of these failing is
  message 232, "name file is stale or damaged". So a damaged or stale
  name file passes `cccomp`, which only copies the stamp, and `ccgen`
  refuses it: later, but nothing is lost (decided in discussion,
  2026-11-08: every byte of cccomp counts, and the pipeline is
  normally run by a script).

## An example

For `t.c` holding the one line `int main() { return 0; }`, `ccpp`
writes `t.i`:

```
43 54 4B 08    "CTK", version 8
80             int
04 1A 00       an identifier: id 26, main
28 29 7B       ( ) {
89             return
05 00 00       the number 0
3B 7D          ; }
01             the next line
00 1F 00 ss ss the end: 31 ids, and the checksum
```

and `t.nam`: the 31 fixed names (`main` is one of them, so no other
name is added), then `@t.c`, a blank line, and `=1 001F ssss`, where
`ssss` is the checksum of the 32 lines before the blank line (`ss ss`
in `t.i`, low byte first).

## Limits

| Limit | Macro | Message |
|---|---|---|
| A line (after joining lines that end in `\`) | `MAX_LINE` | 233 |
| A string literal, after joining | `MAX_STR` | 12 |
| An identifier | `MAX_NAME`; the rest are ignored | none |
| `ccpp`'s name pool: every name and file name, a length byte each | `MAX_PP_POOL` | 18 |
| Files `#include`d at once, beside the source | `MAX_INCLUDE`, or what the system can open | 237, 236 |
| Groups (`#ifdef` ...) open at once | `MAX_IF_NEST` | 243 |
| Macros defined at once, counting `__CCISH__` | `MAX_MACROS` | 244 |
| Their bodies, as tokens (an identifier or number is 3 bytes) | `MAX_MACRO_BYTES` | 244 |
| Macro expansions inside one another, and arguments being expanded | `MAX_EXPAND` | 245 |
| The texts being read and the arguments being collected; the arguments' own expansions | `MAX_EXP_BYTES` | 256 |
| Arguments of the calls being expanded | `MAX_ARG_SLOTS` | 256 |
| A macro's parameters | `MAX_PP_PARAMS` | 257 |
| `-I` directories; `-D` and `-U` options | `MAX_INC_DIRS`, `MAX_PP_OPTS` | 248 |

Their current values are in [doc/limits.md](doc/limits.md), which
`make limits` generates from the sources. They're set from
measurement (`make measure` reports how full each got for ccish's own
sources), with room above, and more room where ccish's own use is
small (its macros are short), so that ordinary programs fit too.

## Versions

- **`NAME.i` version 1, `NAME.nam` version 1** (step 1 of the `ccpp`
  design): the first; step 2 began writing the `$09` tags, and step 4
  the `$02` and `$03` records, all reserved from the start. Step 4
  added four fixed ids (`__CCISH__`, `__FILE__`, `__LINE__`,
  `defined`: 28-31), which a version-1 reader takes as it finds them.
- **`NAME.i` version 8** (2026-11-09): `register` is a supported
  keyword, `$92`, where it was an unsupported one (`$08`).
- **`NAME.nam` version 2** (2026-11-07, phase 4 of
  doc/bootstrap-test.md): two more fixed ids, `__asm__` (32) and
  `__plasma_import` (33), so every other id is 2 higher.

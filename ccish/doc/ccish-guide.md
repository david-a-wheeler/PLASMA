# The ccish guide

This ia a user guide to ccish, a C compiler for the PLASMA virtual machine
running on 6502 computers sich as the Apple II.

1. [What ccish is](#1-what-ccish-is)
2. [Using the compiler](#2-using-the-compiler)
3. [Lexical conventions](#3-lexical-conventions)
4. [Types and values](#4-types-and-values)
5. [Declarations and storage](#5-declarations-and-storage)
6. [Expressions and operators](#6-expressions-and-operators)
7. [Statements](#7-statements)
8. [The preprocessor](#8-the-preprocessor)
9. [What ccish doesn't (yet) do](#9-what-ccish-doesnt-yet-do)
10. [How the compiler works](#10-how-the-compiler-works)
11. [Calling built-in CMD library](#11-calling-built-in-cmd-library)
12. [Writing small and fast code](#12-writing-small-and-fast-code)

CCish is dedicated to the memory of Bob Pew.
It was developed with the assistance of AI, so if that bothers you, don't use it.
Everyone else, enjoy.

## 1. What ccish is

ccish compiles a dialect of C into PLASMA REL modules that run on the
6502, on any of PLASMA's targets (the Apple II through the PLEIDES VM,
and the C VM `plvm` used for testing). The dialect and the compiler
share the name: a program written in it is *C-ish*, and `ccish` is the
compiler. It is intended to aggressively optimize its generated results.

The ccish compiler is designed to run on an Apple II and to compile
itself there. It doesn't yet. ccish compiles its own sources and links
them with its C library (section 11.2), and the self-compiled ccpp
runs under plvm; the bootstrap itself is next. Before the Apple II,
`exit()` must unwind the VM (section 11.2), PLASMA's loaders must
stream a module's RLD, and `cccomp` must shrink by about 4 to 5K to
fit a 64K machine's free memory. The plan is in
doc/bootstrap-test.md, and the memory budget in approach.md, issue 24.

### 1.1 A subset of C, not a standard

ccish does not try to be an exact implementation of any particular
standard or version of C. It is closer in some ways to C89, and in
others it is not. For example, ccish supports `//` comments, which
C89 does not.

The working rule is:

* If ccish supports a construct, it usually has the same semantics as
  C. The same source generally compiles with gcc and means the same
  thing. Where C leaves a choice to the implementation, ccish's
  choices are fixed and listed in this guide: `int` is 16 bits, `char`
  is unsigned, and so on (section 4).
* ccish is a subset, so it does not include everything in any version
  of C. Parts that are hard to implement, or costly in space on a 64K
  machine, and not needed for ccish to compile itself, are left out
  on purpose: floating point, `long`, `double`, bitfields, and
  structs over 8 bytes passed or returned by value, among others
  (section 9). Using one is a clear compile-time error, not a
  silent miscompile.
* If strict compatibility ever has to give way so the compiler fits
  in 64K, the goal is an incompatibility that is documented here and
  unlikely to affect real code. The known differences so far:
  * A decimal constant over 32767 (such as `40000`) is the `int`
    with those bits, not a `long` (ccish has no `long`); write
    `40000u` for the `unsigned` one.
  * `unsigned char` is plain `char`: ccish doesn't tell the two
    apart (both are unsigned), so `unsigned char *` and `char *`
    mix freely.
  * Mixing signed and unsigned gives no warning (gcc's
    `-Wsign-compare` would).
  * `enum tag` isn't checked against a definition, so ccish accepts
    some wrong programs (a variable of an enum type never defined).
  * `struct W` and `union W` share one tag name space, so a name
    can't be both (the standard C lets them coexist; section 4.11).
* **The small build, for the Apple II, doesn't fully conform to the
  C standard:** to fit in 64K it intentionally leaves out some
  compile-time checks the standard requires, so it accepts some
  invalid programs (section 2.7). This never affects a correct
  program, which compiles to exactly the same code in both builds.
  The full build, on the host, makes every check.

### 1.2 Self-hosting on the Apple II

ccish is specifically designed to be able to self-host: the compiler
compiling itself on an Apple ][ with 64K of RAM. That goal shapes
everything:

* The compiler is written in valid, portable C, so it compiles under
  gcc and clang as well as under ccish itself. The host build is the
  test oracle for the Apple II build.
* The compiler runs as a pipeline of small programs (section 10), so
  on the Apple II each pass runs separately and has nearly all of
  memory to itself.
* Every table is a fixed size set just above what ccish's own source
  needs; going past one is a clear error (section 9.3).
* The same source makes two builds (section 2.6): the full build,
  with all the checks, and the small build for the 64K machine. For
  a correct program, the small build writes exactly the same objects
  and modules; it leaves out some checks of incorrect ones (section
  2.7).

### 1.3 Where it fits

ccish's output is a PLASMA module. It runs under `plvm` on the host
for testing, and under PLASMA's 6502 VMs on the Apple II, Apple 1,
C64 and the JIT target. A ccish program may use CMDSYS functions
(`__puts`, `__putc`, `__puti`, `__heapalloc`, and the rest, section
5.3), and
it can share data with PLASMA: a ccish `struct` has the same layout
as PLASMA's `struc` (members in order, no padding), and a struct
returned by value is PLASMA's multi-value return, so
`struct qr { int q, r; }` matches PLASMA's `divmod()#2`.

The design and the plan for growing the language are in
[approach.md](../approach.md); the VM itself is described in
[../doc/Bytecodes.md](../../doc/Bytecodes.md); the file formats in
[CCPP.md](../CCPP.md) and [OBJECT.md](../OBJECT.md).

### Quirks

A few special quirks:

* It doesn't have C's standard library yet, but it can call the
  PLASMA CMD library for its platform, as noted below. Those routines
  have their PLASMA names with `__` in front (`__puts` for PLASMA's
  `puts`), so the standard names (`puts`, `memset`, ...) are free for
  C's own functions (section 5.3).
* If you pass strings to CMD routines like `__puts`,
  you must send *PLASMA* strings. The special call
  `pstr(...)` converts constant C strings to PLASMA strings.
  This means you must say `__puts(pstr("Hello"));` and not
  `__puts("Hello");`. Omitting the `pstr()` will typically print
  garbage.
* The `int` type is a signed 16-bit that always wraps.

## 2. Using the compiler

### 2.1 Compiling a file

```
ccish [-l] [-d] [-i] [-k] [-o output] [-I dir] [-D name[=text]] [-U name] input.c [object...]
```

* `-o output`: the module file to write. The default is the input name
  without `.c`. To run a module with `plvm`, give it a lowercase name
  with no extension, and run `plvm name` from its directory.
* `-l`: print a listing of the generated bytecode (full build only).
* `-d`: print the machine ops after the peephole stage, that is the
  final code (the code-generation tests compare against this). Full
  build only: it keeps each function's ops in a 12K buffer until the
  function ends.
* `-i`: print the IR that passes from the parser to the back end, as
  the back end reads it, with each function's source line. The IR ops
  are described in [IR.md](../IR.md).
* `-k`: keep the intermediate files: the token stream `output.i` and
  name file `output.nam` (the preprocessor's), the IR file
  `output.ir` (the front end's), and the stream files `output.obj`
  (one stream of tagged records: data, strings and functions) and
  `output.syms` (the symbol summary, the back end's). Normally they
  are removed afterward, and after an error.
* `-I dir`: add a directory to the `#include` search (section 8).
* `-D name[=text]` and `-U name`: define and undefine a macro before
  the first line of the program.
* `object...`: more objects to link into the module after the input's,
  each named as for `ccrel` (section 2.3), such as the C library's
  (section 11.2).

Example:

```
$ cat hello.c
int add(int a, int b) { return a + b; }
int main(void) { __puts(pstr("Hello")); __puti(add(2, 3)); __putln(); return 0; }
$ ./ccish hello.c && ../src/plvm hello
Hello5
```

`__puts` and the rest of the CMDSYS functions are declared already
(section 5.3); for your own functions, each must be declared before
it is called.

### 2.2 The four passes

`ccish` is for the host: it runs every pass in one process, through
the same intermediate files. The passes are also programs of their
own, so that on the Apple II each can run separately:

```
ccpp [-E] [-k] [-o output] [-I dir] [-D name[=text]] [-U name] input.c
cccomp [-k] [-o output] input
ccgen [-i] [-k] output
ccrel [-l] [-o module] output|@file...
```

* `ccpp` (the preprocessor and lexer) reads the source, carries out
  the preprocessor directives, and writes the token stream
  `output.i` and the name file `output.nam`.
* `cccomp` (the parser) reads those and writes the IR file
  `output.ir` (`input.i` is removed unless `-k`).
* `ccgen` (the code generator) reads the IR file and the name file
  and writes the stream files (both are removed unless `-k`).
* `ccrel` (the final pass) links one or more objects into a REL
  module.

Each pass writes what the next reads, and nothing else. The formats
are in [CCPP.md](../CCPP.md) (the token stream and name file) and
[OBJECT.md](../OBJECT.md) (the IR file, the stream files and the
module). On the host you rarely need the separate passes; the
exceptions are linking several files (2.3) and keeping the
intermediates with `-k`.

### 2.3 Linking several files

Compile each file through the passes, then link the objects into one
module:

```
$ ./ccpp main.c && ./cccomp main && ./ccgen main
$ ./ccpp util.c && ./cccomp util && ./ccgen util
$ ./ccrel -o prog main util && ../src/plvm prog
```

* Each object is named by its output name, as given to `ccpp`
  (`main` for `main.obj` and `main.syms`). Objects are linked in the
  order given: every object's globals, then every object's strings,
  then every object's code. `-o` is required with more than one.
* `@file` names the objects in `file` instead, one per line (a CR or
  LF ends a line, and blank lines are skipped): `ccrel -o prog
  @prog.objs`. The VM's command line holds only 127 characters. The
  names may total 512 bytes, and a link may have 32 objects. It's
  GCC's and the linkers' `@file` syntax, but not their format: theirs
  holds arguments, options included, split at white space and quoted;
  ccrel's holds only names, a whole line each.
* `ccrel` keeps the objects, so after a change only the changed file
  needs compiling again. (`ccish` removes the stream files it makes,
  unless `-k`.)
* A function or global that isn't `static` is shared by every
  object; a `static` one is private to its file, so two files may
  each have a `static` function of the same name. A name may be
  defined in only one object ("multiple definition of"), and every
  declaration of it, in every object, must agree with the others: the
  parameter count, whether it returns a value, the types (through a
  type hash) and a global's size ("conflicting types for"). This
  holds even for a declaration nothing uses.
* A function that no object defines must be a CMDSYS function
  (`__puts` and the rest), which is imported (section 5.3), or a
  PLASMA import (section 11.1); a program may define its own function
  of a CMDSYS name, with the same type, and calls then go to it.
  Anything else used and not defined is an "undefined reference",
  including a C library function (`strlen`) whose object isn't on the
  link line (section 11.2).
* `main` (not `static`) in any object is the entry point. A `main`
  with parameters needs the library's `start` object, which calls it
  (section 5.1).
* The module exports nothing. It imports CMDSYS functions, by their
  PLASMA names (`__puts` imports `PUTS`), and what you declare as a
  PLASMA import (section 11.1); other C names are matched in full,
  with their case, and never reach PLASMA's case-insensitive,
  16-character symbol table.

### 2.4 Messages

Every error and warning has a number, printed before its text:

```
hello.c:3: error 41: lvalue required as left operand of assignment
```

The text comes from the message file `ccish.msg`, in the programs'
own directory, read only when a message is printed, so no message
text takes memory. Each line of the file is a number, a space and the
text, in ascending order; the numbers alone are enough to read the
file. Warnings are printed the same way (`file:line: warning 49:
...`), and compilation continues.

Compilation stops at the first error.

### 2.5 Building and testing the compiler

```
make            # builds ./ccish, ./ccpp, ./cccomp, ./ccgen and ./ccrel
make small      # the small build: ./ccpp-small, ./cccomp-small,
                # ./ccgen-small and ./ccrel-small
make test       # also builds ../src/plvm (the C VM), then runs the tests
make measure    # compiles ccish's own source with ccish, links the
                # Apple II programs, and reports their sizes
```

`make test` runs the whole suite: programs that must run and print
their expected output, programs that must fail with their message,
token-stream checks, link checks, and the cross tests, where the same
source must print the same under ccish and under a host compiler
(gcc or clang).

### 2.6 The full and small builds

The same source makes two builds. The full build, the default, has
everything. The small build, for the 64K Apple II, is made with
`-DCCISH_SMALL`: it leaves out `-l` and `-d`, checks that only a
compiler bug or a damaged file would fail (`ccgen`'s check of the
blocks it moves), and the compile-time checks that only an incorrect
program fails (section 2.7). `ccgen -i` is in both. The small
`cccomp` gives no warnings, and each error only its file, line,
number and text: for example `a.c:3: error 64: undeclared
identifier`, where the full build adds `'y'`. Only a few details stay,
those that are the only clue to what's wrong: a function's name, a
file that can't be opened, an undefined symbol, and the program's own
text in `#error` and `_Static_assert`. So the small build should
compile only code the full build (or gcc) has already accepted. For a
correct program it writes exactly the same objects and modules; the
tests check that.

### 2.7 Checks the small build leaves out

The small build doesn't make some of the compile-time checks the C
standard requires: there isn't room for them in a 64K Apple II. This
makes the small build not fully conform to the C standard, on
purpose. It never affects a correct program: a program the full build
accepts compiles to exactly the same code in the small build. But the
small build accepts some incorrect programs, and compiles something
for them, where a conforming compiler must report an error. The full
build, on the host (section 2.6), makes every one of these checks.

These are the checks the small build doesn't make, though the
standard requires a diagnostic for each (C11 clauses given):

1. **Assignment types** (6.5.16.1): assigning, initializing (6.7.9),
   passing an argument (6.5.2.2) or returning a value (6.8.6.4) of an
   incompatible type: a struct of another type, a pointer from an
   integer or the other way around (without a cast), or a pointer to
   an incompatible type; and using the value of a `void` expression
   there.
2. **Pointer comparisons and subtraction** (6.5.8, 6.5.9, 6.5.6):
   comparing a pointer with an integer other than a null pointer
   constant, or pointers to incompatible types, and subtracting
   pointers to incompatible types.
3. **Operand types**: unary `-`, `+` or `~` on a pointer (6.5.3.3);
   a binary operator on operands it doesn't take, such as `*` on a
   pointer (6.5.5 to 6.5.14); a `?:` whose two results have
   incompatible types (6.5.15).
4. **Assigning to an array** (6.5.16, 6.3.2.1), **or to a const
   object**, or to a struct with a const member. (Assigning to a
   const variable whose value is known still fails, as "lvalue
   required".)
5. **Calls** (6.5.2.2): the wrong number of arguments to a function
   with a known parameter count.
6. **Conflicting declarations** (6.7, 6.7.6.3): a global or function
   declared again with a different type, a different parameter count
   or different parameter types; an enum constant, typedef name,
   constexpr name, variable or function declared twice in one scope
   with different meanings; and repeated or conflicting type
   specifiers (`int int`, `char int`; 6.7.2).
7. **Void and function types where they can't be** (6.7, 6.7.2.1,
   6.7.6.3): a variable, struct member or parameter of type `void`,
   and a struct member of function type.
8. **Struct members** (6.7.2.1): two members of one struct with the
   same name, and a member or array element of an incomplete type
   (6.7.6.2).
9. **A parameter without a name** in a function's definition (6.9.1).
10. **`sizeof`** (6.5.3.4) of `void`, or of an incomplete type.
11. **Statements**: duplicate case values, or two `default` labels,
    in one `switch` (6.8.4.2); labels defined twice in one function
    (6.8.1); a goto to a label never defined (6.8.6.1); a `return`
    with a value in a `void` function (6.8.6.4). (A label defined
    twice, and a goto to no label, still stop `ccgen`, with an
    internal error message.)
12. **Integer constant expressions** (6.6): a const variable's value
    where C requires one, as in an array size, a case label, an enum
    value or a static initializer. The small build uses the value, as
    if the variable were `constexpr` (section 5.8).
13. **`register`**: taking a register variable's address (6.5.3.2),
    and `register` at file scope (6.9). Unlike the others, the first
    can make the small build compile an incorrect program wrongly,
    not just accept it: ccgen relies on no pointer reaching a
    register local (section 5.2).

The small build also doesn't reject these, for which C requires no
diagnostic (the behavior is undefined), so leaving them out doesn't
change conformance; the full build reports them: division by a
constant 0, or a shift by a constant outside 0 to 15; `static` after
a declaration that isn't (6.2.2); and a `main` with a parameter
count other than 0 or 2 (5.1.2.2.1).

To find these errors, compile with the full build of ccish on a host
(section 2.6), or with another C compiler, such as `gcc -fsyntax-only
-Wall` or `clang -fsyntax-only` (section 1.1 has what to expect of
ccish's C under them). The checks the small build still makes are
those of anything ccish can't compile, valid C or not, since without
them it would compile a valid program wrongly: syntax, the
constructs it doesn't support, its limits, two definitions of one
global, and variables and by-value parameters and results of an
incomplete type.

## 3. Lexical conventions

### 3.1 Identifiers

An identifier is a letter or underscore, followed by letters, digits
and underscores. Only the first 31 characters are significant; the
rest are ignored. A keyword is not an identifier.

The supported keywords are:

```
break    const    continue default do     else     enum   extern
for      goto     if       int    register return  sizeof   short
static   struct   switch   typedef  union    unsigned void   volatile
while    _Static_assert   constexpr
```

plus `signed`, as in `signed int`, which is `int`, and `short`, a
synonym for `int` (section 4.1). A `union` is a tagged type as `struct`
is (section 4.11). Four more of C89's keywords are recognized so the
error is clear, but not supported (section 9.1): `auto`, `double`,
`float` and `long`.

`enum tag` used as a type does not have to match a definition
(section 1.1).

### 3.2 Comments and digraphs

There are two kinds of comment:

* `/* ... */` to the next `*/`. They do not nest.
* `// ...` to the end of the line (this is not in C89).

A comment is replaced by one space, so it separates tokens just as
whitespace does. An unterminated `/*` comment to the end of the file
is an error.

The C95 digraphs are supported, for keyboards that lack the symbols:

```
<%  %>      {   }
<:  :>      [   ]
%:          #
%:%:        ##
```

Trigraphs are not supported.

A line ends with LF, CR or CRLF; all three are accepted in the same
file.

### 3.3 Integer and character constants

Integer constants are decimal, octal (leading `0`) or hexadecimal
(`0x`), each with optional `u` or `U` suffix. A constant must fit in
16 bits ("number too big"), except:

* a decimal constant over 32767 is the `int` with those bits (see
  section 1.1; there is no `long` to widen to), and
* a hex or octal constant that doesn't fit in an `int` is
  `unsigned`, as in C: `0x8000` is `unsigned`.

A constant is `unsigned` if it has a `u` suffix, or by the rule
above; otherwise it is `int`.

Character constants are `'a'`, `'\\'`, and the escapes:

```
\\   \'   \"   \?
\a \b \f \n \r \t \v      the standard escapes
\OOO   octal, up to three digits 0-7
\xHH   hex, as many digits as fit, value kept to a byte
```

Any other `\` sequence is an "unknown escape sequence". A character
constant has type `int` (the character's value, 0-255, since `char`
is unsigned), as in C. Multi-character constants like `'ab'` are an
error.

### 3.4 String literals

A string literal is `"..."` with the same escapes. Adjacent string
literals are joined into one. Each has at most 255 characters after
joining, so that every literal is a PLASMA string as well: it is
stored as a length byte, the characters, then a NUL.

In C the literal's value is the address of the first character, with
type `char *`. The special routine `pstr("text")` when given a
C constant string gives the address of the length byte
instead, the PLASMA (length-prefixed) form that CMDSYS routines such
as `__puts` expect. `pstr()` accepts only a string literal, and its
type is `char *` too.

## 4. Types and values

The arithmetic types are `int` (16 bits), `char` (8 bits, unsigned)
and `unsigned` (16 bits); `void` names the absence of a value.
Pointers are 16 bits. The aggregates are `struct` (section 4.7) and
`union` (section 4.11). `short` is a synonym for `int` (section 4.1);
there is no `long`, no floating point, and no `signed char`
(section 9.1).

All values live in 16-bit words on a little-endian 6502. Where C
leaves a choice to the implementation, ccish's choices are the ones
listed here.

### 4.1 int: 16 bits, with wraparound

`int` is a 16-bit signed integer, from -32768 to 32767, in two's
complement.

`short` is a synonym for `int`: the same type in every position,
`short` local, `short` parameter, `short` return, `short *`,
`short` array element, `short` struct member, and in a macro or a
cast. The standard C distinction between the two (a `short` that may
be narrower than an `int`) is not worth implementing: on this target
an `int` is already the narrowest useful word type, and ccish has to
run in limited memory. The synonym also makes it easier to write a
program that compiles under both ccish and a regular C compiler, and
to port existing programs that use `short` for small integers.
`sizeof(short) / sizeof(int)` is 1 under ccish; under a regular
compiler it may be 0, so a portable program uses the element-count
ratio (`sizeof a / sizeof a[0]`) instead.

Integer arithmetic wraps around on overflow, and that is defined
behavior: `32767 + 1` is `-32768`, and `-32768 - 1` is `32767`.
Wraparound is not undefined behavior in ccish. (When you compile the
same source with gcc or clang to compare, use `-fwrapv` so that they
assume the same wraparound; their `int` will be a different size, so
values must stay within -32768 to 32767 anyway, as the cross tests'
contract in `inc/ptest.h` says.)

`/` on signed values truncates toward zero (`-7 / 2` is `-3`), and
`>>` on a negative value is an arithmetic shift (`-8 >> 1` is `-4`).
Division by zero is undefined: a constant zero divisor is a compile
error, and a run-time zero divisor is not checked (it hangs on most
6502 targets, which is what the 6502 does on a divide by zero).

Comparisons of `int`s are signed. `!`, `&&`, `||` and the relational
and equality operators give 1 or 0, as in C (PLASMA's own
comparisons give `$FFFF` for true; the compiler normalizes).

### 4.2 char: 8 bits, unsigned

`char` is one byte, unsigned, 0 to 255. `unsigned char` is the same
type; ccish doesn't tell the two apart, so `unsigned char *` and
`char *` mix freely. `signed char` is not supported yet.

A value becomes a `char` by keeping its low byte when it is stored
into a `char`, passed as a `char` parameter, returned from a `char`
function, or cast to `char`. In expressions, a `char` is promoted to
`int` first, as in C, so `char c = 200; c + 100` is the `int` 300,
and storing it back keeps 44.

Byte loads (`LB`, `LLB`, `LAB` in the VM) zero-extend, so an unsigned
`char` costs nothing, which is why the choice was made (approach.md,
issue 7). A signed `char` would need sign extension on every load.

### 4.3 unsigned

`unsigned` (`unsigned int`, `int unsigned`) is 16 bits, 0 to 65535.
As in C, when an `int` meets an `unsigned` in an operator, the `int`
becomes `unsigned` first (so `-1 < 1u` is 0: `-1` becomes 65535).
An integer constant is `unsigned` if it has a `u` suffix, or is hex
or octal and too big for an `int` (`0x8000`, section 3.3).

Comparisons, `/`, `%` and `>>` on `unsigned` values are unsigned.
`+ - *`, `==` and `!=` work the same for both. Unsigned
comparisons cost a little extra code (a sign-bit flip of each side,
or a call for some forms; see section 12.4).

By a constant, `>>` (and `/` and `%` by a power of two) are done
inline; otherwise they call small helper functions that the compiler
adds to the module (one copy each, however many files use them).

### 4.4 void

`void` is the type of a function that returns no value, and of a
pointer to unknown type. `void` can't be a variable's type. A `void`
function returns no value; any other function returns exactly one.
Casting to `(void)` discards a value. `void *` converts to and from
any object pointer type without a cast; pointer arithmetic on a
`void *` is an error.

### 4.5 Pointers

Pointers are 16 bits, as every address on the target is.

* `&` takes the address of a variable (or of `*p`).
* `*` reaches what a pointer points to, for reading or storing.
* Pointer arithmetic counts in elements, as in C: `p + i`, `i + p`,
  `p - i`, `p - q`, and `++`, `--`, `+=`, `-=`.
* Pointers compare unsigned (`< > <= >= == !=`), since addresses at
  or above `$8000` are normal. Test pointers with `==` and `!=`
  rather than ordering (section 12.4).

Types are checked strictly (GCC 14 does the same by default): mixing
integers and pointers needs a cast, except for the constant 0, and
pointer types must match unless one is `void *`. This applies to
assignments, initializers, arguments, return values, comparisons and
`?:`.

Addresses at or above `$C000` are I/O, not memory (the speaker is
`$C030`). A pointer to such an address is read and written directly
with the VM's absolute ops: with
`volatile char *const sp = (volatile char *)0xC030;`, `*sp = 65` is
`SAB $C030 65`, and ccish never drops or reorders such accesses
(`volatile`, section 4.10).

### 4.6 Arrays

An array is declared `int a[10]`, `char *names[4]`, `int m[3][4]`,
`struct pt pts[8]`, and so on. The size is a constant expression.

* `a[i]` is `*(a + i)`, as in C (so `i[a]` works too).
* An array used as a value is a pointer to its first element.
* An array can't be assigned to, and there is no array `+` or `-`
  between arrays.
* A local array must fit in the function's 255-byte frame, with the
  other locals; a bigger one can be global or a `static` local
  (section 5.5).
* An array parameter is a pointer, as in C: `int a[]` and `int a[10]`
  are `int *a`, and `int m[][4]` is a pointer to arrays of 4 ints.

### 4.7 Structs

A struct groups members, laid out in order with no padding (the 6502
has no alignment rules), in the same order and at the same offsets as
PLASMA's `struc` lays them out, so C can read PLASMA's data
structures:

```c
struct pt { int x, y; };
struct pt p;         /* 4 bytes: x at 0, y at 2 */
```

A member may have any complete type: another struct, an array, a
pointer (to its own struct too, for a linked list:
`struct node { int v; struct node *next; };`). A struct is declared
`struct tag { ... };` at file scope or in a block. A tag can be
declared first and defined later (`struct s;`), and a tag defined in a
block is visible only until the block ends. A struct whose definition
hasn't been seen yet is incomplete: a pointer to it is fine, but not
a variable, `sizeof`, `->` or pointer arithmetic. Using a tag for the
first time in a parameter list gives a warning, as in gcc, since it
is visible only there.

Access:

* `s.m` is a member of a variable. It compiles like a variable:
  `s.m` on a local is one load.
* `p->m` is a member through a pointer: the pointer's value plus the
  member's offset. `p->m` is a load of the pointer, the offset, an
  add and a load (and just the pointer's load at offset 0). Through a
  constant pointer, such as a block of I/O registers, the offset is
  added at compile time.
* `&s.m` gives a member's address, a pointer of the member's type.

Assignment copies the struct (`a = b`, `*p = s`, `c = b = a`). The
result is the struct assigned to, which isn't an lvalue, as in C.
Between two variables it is a word move per member word when that is
no bigger; otherwise, and always through a pointer, it calls
`__memcpy(dst, src, n)`: CMDSYS's, or the program's own if it defines
one, with CMDSYS's type. (A program's own `memcpy` is unrelated.) A
struct with a `const` member can't be assigned as a whole. `==` on
structs is an error, as in C.

Passing and returning: a struct of up to 8 bytes can be passed and
returned by value, as the words of its bytes (first word deepest). A
parameter lands in the callee's frame in its own layout, with no
copy, and a returned struct is a PLASMA multi-value return:
`struct qr { int q, r; }` matches PLASMA's `divmod()#2`. A returned
struct is stored straight into the variable it is assigned to, and
passed or returned on with no code (`return swap(p);`); `f().x` saves
it to a temporary first. Passing a struct needs a prototype. A bigger
struct can't be passed or returned by value; that is an error (a
deliberate limit, to keep the compiler small: pass a pointer
instead).

Initializers: for globals and `static` locals, a flat brace list over
the struct's scalar members in order, with the same forms as an
array's (section 5.6); a local may also be initialized from another
struct (`struct pt b = a;`).

### 4.8 enum

```c
enum color { RED, GREEN, BLUE = 5 };
```

Each constant is an `int` constant expression: it is one more than
the one before unless given a value, and it must fit in an `int`. It
works in `case` labels, array sizes and global initializers. An enum
type is `int`: a variable of an enum type is an `int` variable, and
the constants are `int` constants, not a new kind of value.

`enum` constants have block scope, as in C: one declared in a block
hides a variable of the same name declared outside it, and the other
way around.

### 4.9 typedef

```c
typedef struct node *NodeP;
typedef int Vec[3];
typedef const char *str;
```

A typedef name can be used wherever a built-in type can, with `*`s,
array sizes and qualifiers. A typedef may name a function type
(`typedef int fn_t(int);`), which can then declare a function
(`fn_t g;`) or a pointer to one (`fn_t *p;`). Typedef names have
block scope, as in C.

### 4.10 const and volatile

Both qualifiers go before or after the type (`const char`,
`char const`), and after a `*` (`char *const p` is a const pointer,
`const char *p` a pointer to const).

* A `const` object can't be assigned to or incremented.
* A pointer may gain `const` or `volatile` on what it points to
  (`char *` to `const char *`), but losing one is a warning, as in
  gcc; deeper levels must match (`char **` to `const char **` is an
  error).
* Reading a `const` variable (an `int`, `char` or pointer, not
  `volatile`) whose initializer is a constant gives that value at
  compile time: with `const int N = 10;`, `x < N` loads `x` and
  compares with the constant 10, and `N * 2` is 20.
  `&N` still works. As in C, `N` isn't a constant expression, so it
  can't size an array, initialize a global, or be used in a
  `_Static_assert`, and a `const int` holding 0 isn't a null pointer.
  For a named value that is a constant expression, use `constexpr`
  (section 5.8).
* A `volatile` object is read and written just as the program says:
  no access is dropped, merged, added, or narrowed (C11 5.1.2.3,
  6.7.3). So `x;` reads a volatile `x`; `vx >= '0' && vx <= '7'` reads
  it twice; a counted loop whose limit is volatile reads it each pass;
  and an assignment's value isn't read back from a volatile object.
  It is checked as part of the type. Its use is the I/O registers
  (section 4.5), where `*sp` is still one `LAB`.
* What an access is, C leaves to the implementation. In ccish, a
  struct with any volatile member is passed and copied a word at a
  time with every word volatile, or by `memcpy` if it's large. A
  local volatile's initializer is never dropped, but two chars may be
  stored as one word and the rest zeroed by word or by `memset`: a
  local is in the frame, so it can't be a device register.
* What isn't volatile, ccish optimizes freely, as C allows: a plain
  load has no effect a program may see, so it can be dropped, done
  once instead of each pass, or reused. So **I/O must be volatile.**
  A pointer to an I/O address declared without `volatile`
  (`char *const kbd = (char *)0xC000;`) may lose its reads: `*kbd;`
  emits nothing, and a loop `while (*kbd < 128)` may read it once.
* **A do-nothing loop must have a volatile in it**, or it may be
  removed. A delay loop such as

  ```c
  int i;
  for (i = 0; i < 500; i++)
    ;
  ```

  does nothing a program can see, so C lets a compiler delete it
  (C11 5.1.2.3, 6.8.5p6), and ccish does: it sets `i` to 500 instead
  (and drops that too if nothing reads `i`). Today that's a counted
  loop with constant ends and an empty body; ccish may delete more
  such loops later. To keep a delay, make the loop variable volatile
  (`volatile int i;`), or put a volatile access in the body (such as
  `*SPEAKER;` for a tone): each of its accesses must happen, so the
  loop runs every pass. A volatile counter is slower, as each pass
  stores and loads it in memory, so time the delay with it.

### 4.11 Unions

A union is a struct-like aggregate whose members all start at byte
0, with the union's size the largest member's: the members overlap.

```c
union W { int a; char b; };    /* 2 bytes, the int's */
union W u;
u.a = 0x1234;
u.b;                           /* 0x34, the int's low byte */
u.b = 99;
u.a;                           /* 0x1263, the high byte stands */
```

A union is declared and used as a struct: `union tag { ... };`,
`u.m`, `p->m`, `&u.m`, a pointer to it, an array of them, a member
of a struct, and passing and returning by value (up to 8 bytes, as
a struct, section 4.7). A union's tag has a struct's rules: it can
be declared first and defined later, it has block scope, and it is
incomplete until its definition is seen. Struct and union tags share
a name space, so a name can't be both (the standard C lets them
coexist; a known difference, as with enum, section 1.1).

Initialization is by the first member only, as in C:
`union W u = { 5 };` sets `u.a`, and `u.b` with it, and a second
initializer is an error. Reading a member other than the one last
written gives the value the shared bytes hold; for this subset of
members (ints and chars, little-endian) ccish and the host compiler
agree on it.

### 4.12 Declarators

A declarator may use C's whole syntax, so a declaration a C compiler
accepts, ccish accepts too:

```c
int (*fp)(int);             /* a pointer to a function */
int (*table[3])(int);       /* an array of 3 of them */
int (*pa)[4] = &a;          /* a pointer to an array of 4 ints */
int *(*f(int))(char *);     /* a function returning a pointer to one */
int x, g(int);              /* a variable and a function together */
sizeof(int (*)[4])          /* an abstract declarator in a type name */
```

ccish checks much less of it than a C compiler would, since it needs
little to generate the code. A function type is only its return type:
a call needs that to know what comes back, and each argument's own
type says how many words it pushes. So any parameter list in a
declarator is skipped, except a file-scope function's own (the one
right after its name, `int g(int x)`), which declares its parameters
and checks the calls of it as before. Two function types are the same
when their return types are. A parameter of function type is a
pointer to the function (`void sort(int cmp(int, int))`), as in C11
6.7.6.3.

A function may be declared in a block (`int g(int);`), and so may a
global (`extern int count;`, section 5.5). Either is declared as at
file scope, from there on: C would hide the name again after the
block, and ccish doesn't.

A function's name used as a value is a pointer to the function, and
so is `&f`; you call through a pointer as `fp(x)` or `(*fp)(x)`:

```c
int twice(int a) { return 2 * a; }
int (*fp)(int) = 0;
int (*ops[2])(int);

fp = twice;                 /* or &twice */
n = fp(5) + (*fp)(6);       /* 10 + 12 */
ops[k](n);                  /* through an element, or a member */
```

A function's address is a constant, so it can initialize a global or
a static, a table of them, or a struct's members:

```c
int (*ops[])(int, int) = {add, sub, &mul, 0};
struct io { int (*open)(int); int (*close)(int); } dev = {d_open, d_close};
```

Since a function type is only its return type, a call through a
pointer checks neither its argument count nor the arguments' types:
passing the wrong number corrupts PLASMA's stack, as it would in K&R
C.

A call through a pointer is PLASMA's `ICAL`, which takes the address
on top of the arguments, so the pointer is loaded last: `fp(x)` is
`LLW x LLW fp ICAL`, and `table[i](x)` loads the element after `x`
the same way. C leaves the order open, so that's allowed. A pointer
that only a call gives (`pick(k)(x)`) is kept in a temporary while the
arguments are computed, which costs 4 bytes. A named function called
as `(*f)(x)` or `(&f)(x)` is a plain `CALL`. A function's address is
its loader stub's, the same wherever it's taken, so pointers to one
function compare equal across files.

## 5. Declarations and storage

### 5.1 Functions

A function is defined with its return type, name, parameters and a
body:

```c
int add(int a, int b) { return a + b; }
```

A function must be declared (by a prototype, or its definition)
before it is called; C89's implicit declarations are an error.
Prototypes and definitions must agree on the return type and the
parameters' types. For a function you call from another file, put the
prototype in a `.h` file and `#include` it: the declaration must be
exact, with the parameter types, so that a mismatch is caught at
compile time instead of becoming a conversion problem at run time.

A few specifics:

* The return type can be any of these types but an array.
* Parameters can be any type but `void`. An array parameter is a
  pointer, as in C (section 4.6).
* `main` takes no parameters (`int main(void)`), or two (`int
  main(int argc, char **argv)`). With two, link the library's `start`
  object (section 11.2): ccrel's init calls its `__start`, which splits
  CMDSYS's command line at spaces, the module's name first, and calls
  `main`. It keeps at most 24 words (`MAX_ARGS`). The parameters'
  types aren't checked. Its return value,
  `& 255`, is the program's exit status: the module's init returns its
  negation, PLASMA's error convention, so an Apple II shows a nonzero
  status as `ERR:$xx`. plvm always exits 0; to get the status on the
  host, run the module with `tools/ccrun` (approach.md issue 22).
* A struct parameter or return value must be at most 8 bytes
  (section 4.7). Passing a struct needs a prototype.
* A function can call itself.
* A function that is called, or whose address is taken, but not
  defined in any file linked into the module must be a CMDSYS
  function (section 5.3).

### 5.2 Local variables

Locals are declared anywhere in a block, with optional initializers:

```c
int a, b = 1, *p = &a;
```

* They have block scope, and an inner block may shadow an outer name
  (as in C). A local or parameter may hide a global of the same name.
* As in C99, the first clause of `for` may declare variables, which
  are in scope only in the loop:
  `for (int i = 0; i < n; i++)`.
* A local's initializer runs each time the declaration is reached.
  A local array's initializer takes the same forms as a global's
  (section 5.6), but its elements may be any expressions
  (`int v[2] = {x, f(x)};`).
* Locals live in the function's frame, at most 255 bytes for the
  whole function, parameters and all. A buffer too big for the frame
  can be a `static` local (section 5.5) or global.

A local is not a slot on the evaluation stack; it has a fixed place
in memory for the duration of the call. A function that has parameters
or locals starts by reserving its own *frame*, and each parameter and
local has a fixed offset in it. A call pushes the arguments' *values*
(one word each, left to right) onto the evaluation stack, and the
callee's first instruction moves them off that stack and into the
callee's own frame. The callee's frame is reserved separately, in
effect a stack of frames, so the caller's locals are not moved or
overwritten while the callee runs; when the callee returns, the
caller's frame is exactly as it was. (The VM opcodes are `ENTER` and
`LEAVE`, with the frame pointed to by `IFP`; [../doc/Bytecodes.md](../../doc/Bytecodes.md),
"Calls and frames".)

Two consequences, both as in C:

* Taking a local's address (`&a`, or an array's name) gives an address
  that stays valid for as long as the call is in progress:

  ```c
  void bump(int *p) { *p = 7; }
  int main(void) {
    int a[5];
    bump(&a[0]);      /* writes 7 into main's frame */
  }
  ```

* Recursion works as expected: each call reserves its own frame, below
  the previous one's, so each call's locals are separate. The depth is
  limited only by the evaluation stack (section 9.3).

A local may be declared `register` (`register int i;`, also in a
`for`: `for (register int i = 0; ...)`). It is an ordinary local, in
the frame, but C forbids taking its address (C11 6.5.3.2: `&i` is an
error, though `&p[1]` is fine for a register pointer `p`), so no
pointer can ever reach it. ccish uses that: in a loop over register
locals, nothing but their own names can change them, even code later
in an outer loop, or a call. So such a loop may be counted though it
is nested in another and stores through a pointer (section 12.2), may
end at `!=`, and has its costly invariant expressions computed once,
before it (section 12.4). Parameters can't be `register` yet, nor can
a file-scope variable (C forbids that).

### 5.3 Calling CMDSYS

CMDSYS's functions have their PLASMA names with `__` in front:
`__puts` is PLASMA's `puts`, `__memset` its `memset`, and so on. C
reserves names that start with `__` to the implementation (C11
7.1.3), so they can't collide with a program's own names, and the
standard names (`puts`, `memset`, `strcpy`, ...) stay free for
functions with C's meanings (approach.md decision 35). The linker
drops the `__` when it writes the import, so the module imports
PLASMA's `PUTS`.

A function that is called, or whose address is taken (`vp = __puti`),
but not defined in any file linked into the module must be a CMDSYS
function; it is imported from CMDSYS, and a declaration must match its
exact types (the compiler checks calls
against them, so a mistake is caught at compile time and not at load
time). The CMDSYS functions are declared already: a call or use with no
declaration is auto-declared from the table in `src/parse.c`
(`cmdsys_sigs[]`), or you may write the declaration yourself, as the
test header `inc/ptest.h` does:

```c
extern void __puti(int);
extern void __putc(char);
extern void __putln(void);
extern void __puts(const char *);
```

(ptest.h then `#define`s `puti` as `__puti` and so on, so that its
tests use the same names under ccish and under gcc.) A plain name is
not a CMDSYS function: calling `puts` with no declaration is an error
("function called without a declaration").

The common ones, with their C types:

```c
void __putc(char c);        /* write one byte */
void __putln(void);         /* the line break */
void __puts(const char *s); /* write a PLASMA string */
void __puti(int n);         /* write a signed decimal integer */
void __putb(int n);         /* binary */
void __puth(int n);         /* hex */
int  __getc(void);          /* read a byte */
int  __heapavail(void);     /* the heap's free bytes */
void *__heapalloc(int n);   /* allocate n bytes */
int  __heaprelease(void *p);
void *__heapmark(void);
void __memset(void *p, int c, int n);  /* fill with a 16-bit pattern */
void __memcpy(void *d, const void *s, int n);
char *__strcpy(char *d, const char *s);
char *__strcat(char *d, const char *s);
int  __toupper(int c);
int  __sext(int v);         /* sign-extend the low byte */
int  __isult(int a, int b); /* unsigned a < b, as -1 or 0 */
int  __isuge(int a, int b);
int  __isugt(int a, int b);
int  __isule(int a, int b);
```

The full list and types are `cmdsys_sigs[]` in `src/parse.c`.

These are PLASMA's functions with PLASMA's meanings, not C's. For
example, `__puts` takes a PLASMA string (length-prefixed, so build one
with `pstr`) and adds no newline, and `__memset` fills with a 16-bit
pattern, so `__memset(p, 'a', 4)` gives `a`, NUL, `a`, NUL (with 0,
as ccish uses it, that makes no difference). A pointer that a function
only reads is `const` (`__puts`, the source of `__strcpy`, `__strcat`
and `__memcpy`), so passing a `const char *` needs no cast.

For C's meaning, use the standard name: ccish has no C library yet,
so for now you define it yourself, as an ordinary function, which may
call the CMDSYS one. tests/run/c_names.c defines `puts` (C's: a C
string, then a newline) and `memset` (C's: a byte fill) beside
`__puts`:

```c
int puts(const char *s)
{
  while (*s)
    __putc(*s++);
  __putc('\n');
  return 0;
}
```

A program may also define its own function of a CMDSYS name, with
the same type, and calls then go to it instead of the import. The
compiler's own calls use these names too: a struct copy calls
`__memcpy` (section 4.7).

### 5.4 Globals

Global variables are declared outside functions, anywhere before
their first use:

```c
int count, *p, buf[1000];
```

They are private to the module (not exported), though shared by the
files linked into it. A global can't share a name with a function or
another global, but a local or parameter may hide it.

A global without an initializer is zero. A large one without one
(an array or struct of 256 bytes or more, whose address no
initializer holds) isn't in the module file at all: the module takes
it from the heap when it loads and zeroes it, so the file is smaller,
and so is the memory needed to load it. It is reached through a
pointer, a little slower (OBJECT.md, "Zero-filled globals"). If the
heap can't hold such globals, the program prints `not enough memory`
and doesn't run.

### 5.5 extern and static

`extern`, at file scope:

```c
extern int count;
extern char buf[];
```

declares a global without giving it storage. A later definition in
the same file (`int count = 1;`) gives it storage; otherwise it must
be defined in another file linked with it (section 2.3), or it is an
"undefined reference". One that is never used needn't be defined.
`extern` on a function declaration changes nothing. An `extern` with
an initializer isn't supported. `extern` in a block declares the
global from there on, as at file scope (section 4.12); a local of the
same name in scope there is an error, since it would hide the global.
`sizeof` an array of unknown size is an error.

`static` has two uses:

* A static local (`static int n = 10;`) keeps its value between
  calls. It has block scope, like any local, but lives in the data
  segment, like a global, so it takes no frame space: a buffer too
  big for the frame can be a static local (`static char line[300];`).
  Its initializer takes the forms a global's does, must be constant,
  and is applied once, when the module loads. A `for` loop's
  declaration can't be static, as in C.
* On a global or a function, `static` makes it local to the file
  (section 2.3). As in C, `static` after a declaration without it is
  an error, and a later declaration without `static` (or with
  `extern`) keeps it static.

### 5.6 Initializers

Global and static-local initializers are computed at compile time and
stored in the module, so they cost no code. The forms:

* Integers and `char`s: any constant expression
  (`int n = 3 * 4;`, `int size = sizeof(int[10]);`).
* Pointers: 0, or an address constant: a string literal or a global's
  (or `static` local's) address, with any constant indexes and
  members (`&m[1][2]`, `&s.y`, `&ps[1].x`), an array's name, casts to
  pointer types, and `+` or `-` a constant (`buf + 10`, `2 + buf`,
  `"abc" + 1`, `(char *)0xC030`, `(char *)0x400 + 128`, which is
  `(char *)0x480`; an `int *` adds twice the constant). The expression
  parser folds these in code too, so `buf + 10` is one address push. Casting an address
  to an integer (`(int)&x`) gives "initializer is not a constant
  ccish supports".
* Arrays: a brace list (`int t[5] = {1, 2, 3};`), with the rest zero
  and an optional trailing comma. The size may come from the list
  (`int t[] = {1, 2};`). A `char` array may be a string
  (`char s[] = "hi";` has 3 elements, and so does `{"hi"}`), and so
  may each row of a `char` array of arrays
  (`char kw[][6] = {"int", "char"};`).
* Structs: a brace list over the members in order
  (`struct pt p = {1, 2};`). A `char` array member may be a string
  (`{"nut", 40}`). A union's list initializes its first member.
* Arrays of arrays or structs, and structs holding them, may have a
  brace list for each element or member, at any depth, as in C:
  `int m[2][3] = {{1, 2, 3}, {4}};`,
  `struct pt ps[] = {{1, 2}, {3}};`. A short inner list leaves the
  rest of its element zero (`m[1]` is 4, 0, 0, and `ps[1].y` is 0),
  and `{}` is all zero. The braces may also be left out (brace
  elision): `{1, 2, 3, 4}` fills a `struct pt ps[2]` in order. The two
  mix as C says: where the next item is `{`, it starts the list of the
  subobject about to be initialized, so `int m[2][2] = {1, {2}, 3};`
  is 1, 2, 3, 0 (gcc warns about such mixes).
* A scalar may be in braces (`int x = {5};`), also inside a list.
* Designated initializers (C99's `.x = 1`, `[2] = 5`) aren't
  supported.

Local initializers may be any expression, and run when the
declaration is reached.

### 5.7 _Static_assert

```c
enum { T_FIRST = 0x20, T_LAST = 0x3F };
constexpr int WIDTH = 40;
_Static_assert((T_FIRST & 0x1F) == 0, "T_FIRST starts a block of 32");
_Static_assert(WIDTH % 8 == 0, "WIDTH is whole bytes");
```

A `_Static_assert` checks a constant expression when the program is
compiled. If it's 0, the compile stops with "static assertion failed:"
and the message. A true one costs nothing: it emits no code, and its
message takes no space. Use them to state what code relies on, such
as the values of constants chosen for bit tricks, next to the
constants.

As in C, the expression must be an integer constant expression:
integer and character constants, enum constants, `constexpr` names,
`sizeof`, casts, and the operators over them. A `const` variable's
value doesn't count (gcc rejects it too), so **to use a named value
in a `_Static_assert`, declare it `constexpr`** (section 5.8), or use
an enum constant or a macro.

To compare constants of two different enums, cast them to `int`
(`(int)A == (int)B`): gcc warns about comparing two enums' constants,
and ccish's own sources build with gcc too.

What ccish leaves out of C11's `_Static_assert`:

* It may be at file scope or in a block, but not in a `for`
  initializer or in a struct or union's member list.
* A pointer constant expression (`&g != 0`) isn't accepted: the
  expression must be an integer.
* The `;` after the `)` is required, as gcc and clang require it.

### 5.8 constexpr

```c
constexpr int SIZE = 8;
constexpr unsigned MASK = 0xF000;
char buf[SIZE];
_Static_assert((MASK & 0x0FFF) == 0, "MASK is the top 4 bits");
```

C23's `constexpr` names a constant. Its value must be a constant
expression (a `const` variable's value isn't one, but another
`constexpr`'s is), and its name is then a constant expression too, so
it works wherever a constant does: a `_Static_assert`, an array size,
an enum value, a case label or a global initializer. It has its type:
`MASK` above is unsigned, so it compares as unsigned. It has block
scope, as a variable does, and a `constexpr` at file scope is local to
the file, so a header may declare one and each file that includes it
gets its own.

ccish makes a `constexpr` a named constant, as an enum constant is, so
it takes no storage at all: using `SIZE` compiles exactly as using
`8` does. That's why it differs from C23, which makes it a read-only
object:

* It has no address: `&SIZE` is an error ("lvalue required").
* It must be a `char`, `int` or `unsigned` (with any qualifiers). A
  `constexpr` pointer, array, struct or union is "not supported yet".
* Its value isn't checked to fit its type: C23 rejects
  `constexpr char C = 300;`, but ccish accepts it, and `C` is 300.
* It may not have a storage class (`static constexpr`, which C23
  allows).
* One declaration may name several (`constexpr int A = 1, B = 2;`),
  where C23 and gcc allow only one. Write one per declaration if the
  code must also build with gcc.

gcc 15 accepts `constexpr` by default (its default is C23); clang
needs `-std=c23`.

## 6. Expressions and operators

Expressions have C's precedence and associativity. From strongest
binding to weakest (operators on one line bind alike, left to right
except where noted):

```
()  []  ->  .  postfix ++  postfix --           postfix
!  ~  unary +  unary -  *  &  sizeof  (type)    unary, right to left
*  /  %
+  -
<<  >>
<  <=  >  >=
==  !=
&
^
|
&&
||
?:                                          right to left
=  +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=  right to left
```

The comma operator is not supported (section 9.1).

### 6.1 Arithmetic

`+ - * / % << >>` on integers.

* `int` arithmetic wraps (section 4.1): there is no overflow trap and
  none is needed.
* `/` and `%` truncate toward zero, for both signs (`-7 / 2` is
  `-3`, `-7 % 2` is `-1`). A constant zero divisor or a constant
  shift count outside 0-15 is a compile error, except in code a
  constant makes unreachable; a run-time zero divisor is not checked.
* `>>` on a negative `int` is an arithmetic shift (it keeps the sign);
  on `unsigned` it is logical.
* `<<` shifts in zeros; the shift count must be 0-15.
* Multiplying by 0, 1, 3 or a power of two needs no multiply
  (section 10.3); the rest use the VM's `MUL`, a 16-step loop.

The usual C rules for signed and unsigned operands apply: when an
`int` meets an `unsigned` in `* / % + - << >> & ^ |`, the `int`
becomes `unsigned` first (section 4.3).

### 6.2 Comparisons and logic

* `< > <= >= == !=` compare signed for `int`s, unsigned for
  `unsigned` values and for pointers (section 4.5).
* `!` is logical negation; `&&` and `||` are logical and/or, and they
  evaluate only the operands C says they do: in `a && b`, `b` is
  evaluated only if `a` is nonzero; in `a || b`, `b` only if `a` is
  zero.
* Comparisons, `!`, `&&` and `||` give 1 or 0, as in C.
* `c ? a : b` evaluates only the branch taken; the two branches must
  be the same type (a `void` branch is an error).

### 6.3 Assignment and increment

* `=` assigns; the left side must be an lvalue (a variable, `*p`,
  `a[i]`, `s.m`, `p->m`), not a constant, a cast, or a function's
  result.
* The compound forms `+= -= *= /= %= &= |= ^= <<= >>=` are
  supported, and assign to the same lvalues.
* `++` and `--`, prefix and postfix: `++i`, `i++`, `--i`, `i--`, on
  any lvalue (a `const` object is an error). The prefix form gives
  the new value, the postfix the old one, as in C.

A store through a computed address (`*p++ = x`, `*f() = x`,
`a[i + 1] = x`) needs the value before the address is disturbed; the
compiler moves the value's code ahead of the address's. A compound
assignment or `++` that would need the address twice
(`*p++ += x`, `(*f())++`, `a[i] += x` with a variable `i`) saves the
address in a hidden local for that statement, which is 1 byte smaller
than computing it twice and computes it once, as C requires.

### 6.4 sizeof

`sizeof expr` and `sizeof(type)`, where a type may have `*`s and
array sizes (`sizeof(char *[4])`). The expression isn't run. The
result is an `unsigned` (C's `size_t`), and is a constant, so it can
size an array. An array gives its full size, a string literal its
length plus 1 (the NUL), and an array parameter 2 (it is a pointer).
`sizeof(void)` and the `sizeof` of an incomplete type are errors.

### 6.5 Casts

A cast converts between any of these types:

```c
char c = (char)x;          /* keep the low byte */
int *p = (int *)n;         /* int to pointer: needs the cast */
const char *s = (const char *)p;
(void)g();                 /* discard a value */
```

The direction matters:

* integer to `char`: keep the low byte (section 4.2);
* integer to pointer and pointer to integer: the address is re-typed
  (a cast is needed to mix integer and pointer at all, except the
  constant 0, which is a null pointer of any pointer type);
* `int` to `unsigned` and back: the bits are unchanged;
* to a pointer type: allowed from `void *`, from any pointer (with a
  cast, or when one is `void *`), and from the constant 0.

Casting to `(void)` discards a value. Casting an address to an
integer in an initializer is an error (section 5.6), as is casting a
struct (`struct` has no integer form).

### 6.6 Constant folding

Operations on constants are computed at compile time with these same
rules (16-bit wrap, truncating division, and so on), so `-(1 << 4)`
and the constant parts of `2 * x + 3 * 4` need no run-time work.
Reading a `const` with a constant initializer folds too
(section 4.10).

Code that a constant makes unreachable is not compiled at all: the
right side of `0 && f()`, the body of `if (0)` or `while (0)`, the
untaken branch of a constant `?:`. One exception: string literals in a statement that
can never run (such as the body of `if (0)`) still take space in the
data segment, since the strings are emitted as the source is read; in
an expression that is never run (such as `0 && f("x")`) they don't.

### 6.7 Evaluation order

Within an expression, ccish follows C's rules for what must happen
when (the side effects of `++`, assignments and calls in the
sub-expressions, and the evaluation of `&&`, `||` and `?:`), but C
leaves the order of evaluation of the operands of most binary
operators unspecified, and ccish does not pick a particular order.
As in C, don't rely on the order: in `f() + g()`, either may run
first, and in `a[i++] = i`, the read of `i` for the index and the
increment are not ordered against each other.

## 7. Statements

Every statement, except the block itself, is terminated by a
semicolon. The statements are: expression statements, empty
statements, labeled statements, blocks, `if`, `switch`, `while`,
`do` and `for`, and the jumps `break`, `continue` and `return`
(`goto` is a labeled statement, section 7.6).

### 7.1 Expression statements

Any expression may be a statement, followed by its semicolon:

```c
x = x + 1;
count++;
__putc('a');
*p = 0;
```

An expression statement is evaluated for its side effects. A value
that is computed and not used is simply dropped. The empty statement
is a bare `;`.

### 7.2 Blocks

A block is a `{ ... }` of declarations and statements. Blocks nest.
A name declared in a block (a local, an enum constant, a typedef, a
struct tag) is visible from its declaration to the end of the block,
and an inner block may shadow an outer name of the same kind
(sections 4.8, 4.9, 5.2).

### 7.3 if and else

```c
if (c) a();
if (c) a(); else b();
if (c) a();
else if (d) b();
else c();
```

The condition is any expression; it is true when nonzero. An `else`
belongs to the nearest unmatched `if`. The code of an `if` whose
condition is a constant false is not compiled at all
(section 6.6). An `if` whose condition is void (a function call that
returns nothing, written where a value is needed) is an error.

### 7.4 while, do-while and for

```c
while (c) body;
do body; while (c);
for (init; cond; step) body;
```

* `while` tests before the body; the body may run zero times.
* `do` runs the body first, then tests; the body runs at least once.
  The `while (c);` at the end is part of the statement.
* `for` runs `init` once, then `cond`, the body, and `step`, in that
  order, until `cond` is false. Any of the three clauses may be
  empty (`for (;;)`, `for (i = 0; ; i++)`, `for (; i < n; )`).
  `init` may declare variables, in scope only in the loop
  (section 5.2).
* `break` leaves the innermost `while`, `do` or `for` (or a
  `switch`, section 7.5). `continue` goes to the test: in a `for`
  with a step, it runs the step first; in a `while` or `do`, it tests
  again. `break` and `continue` outside a loop (and `continue` in a
  `switch`) are errors.

ccish reorders every `while` and `for` so the test sits at the bottom
of the loop (one branch per pass instead of two, sections 10.2 and
10.3), and some loops get PLASMA's counted-loop form, which is faster
still (section 12.2). A `goto` loop is never counted (section 7.6).

### 7.5 switch

```c
switch (e) {
case 1: a();
case 2:
case 3: b();
        break;
default: c();
}
```

`e` is an integer expression; each `case` value is an integer
constant expression (`'a'`, `2 + sizeof(int)`; a `const` variable
isn't one, as in C). Control enters at the matching `case`, or at
`default` if none matches, and falls through until a `break` or the
end of the `switch`. `case` and `default` labels may be anywhere in
the body, even inside nested blocks and loops (Duff's device), and
the cases may be in any order. Duplicate case values, a `case`
outside a `switch`, and a second `default` are errors.

A `switch` compiles to the VM's `SEL` opcode and a case table after
the body, sorted by value, 4 bytes per case. Two limits: the case
values in one `switch` may span at most 32767 (a 6502 VM rule for
`SEL`; "case values too far apart"), and at most 36 cases may be in
the switches open at once.

### 7.6 goto and labels

A label is an identifier followed by `:`, in front of a statement
(with `case` and `default` labels in a `switch`):

```c
int i = 5, sum = 0;
top:
  if (i < 1)
    goto done;
  sum += i;
  i = i - 1;
  goto top;
done:
  __puti(sum);
```

`goto label` jumps to the label. A label is visible throughout its
function, forward and back, but not outside it. You can't `goto`
into the middle of an expression or into a scope where a variable
with an initializer would be skipped; in practice, put labels at
statement boundaries, which is where C puts them too.

A label's own line needs no semicolon; the statement after it is
terminated as usual. `case` labels are a different kind of label
(section 7.5) and don't count as `goto` targets.

`goto` is the tool for state machines and loops whose shape doesn't
fit `for`: the test programs in `tests/run/gotoloop.c` and
`tests/xrun/xgoto.c` are a goto loop and a three-state machine driven
by a chain of gotos.

### 7.7 return

```c
return;        /* in a void function */
return e;      /* e has the function's return type */
```

`return` leaves the function, with `e` as its value (converted to
the return type). A non-`void` function should end in a `return`; if
the last statement isn't one, the compiler adds an implicit
`return` of an undefined value rather than fall off the end. A
`return` inside a loop drops the loop's state first
(section 12.2).

## 8. The preprocessor

`ccpp` (section 2.2) carries out the directives and splits the source
into tokens. A line that ends in a backslash is joined to the next
before anything else, as in C.

### 8.1 #include

```c
#include "file.h"     /* beside the including file, then as <file> */
#include <file>       /* the -I dirs, then the library's headers */
```

After the `-I` directories, ccpp looks in the C library's include
directory (section 11.2): on the host, `lib/include` (its absolute
path, compiled in by the Makefile as `CCISH_INC`); built by ccish and
run under the VM, `ccinc` in CMDSYS's system directory (`syspath`),
which under plvm is `sys/ccinc` in the current directory. A `-I`
directory holding a header of the same name comes first.

Includes nest a few levels deep; the limit is message 236, which
says the file is missing or too many files are open. Each file read
gets its own name in the name file, so messages and `__FILE__`
report the right file.

### 8.2 #define and #undef

```c
#define N 10
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#undef N
```

`#define name tokens` defines an object-like macro;
`#define name(params) tokens` a function-like one (the `(` must be
right after the name, with no blank). The parameters are names, the
last of which may be `...`, which the body uses as `__VA_ARGS__`.

A macro may be defined again only with the same parameters and
tokens. A macro's name is an identifier, or one of the keywords
ccish doesn't support (`long`, say), which are names here too; a
keyword ccish supports can't be a macro name.

A function-like macro's name followed by `(` is a call (the `(` may
be on a later line); otherwise the name stays as it is. Each
argument is fully macro-expanded by itself before it is substituted,
so `MAX(MAX(a, b), c)` works, except next to `#` or `##`, where it is
used as written. The replacement is read again for more macros, with
what follows it, but a macro isn't expanded again inside its own
expansion.

Two operators work on arguments:

* `#param` makes a string of the argument's tokens, spelled again,
  with one space where there were blanks between them. Numbers are
  spelled in decimal (`0x10` as `16`), a string gets its escapes, and
  unprintable bytes come out in octal.
* `a ## b` joins two tokens into one, which must be a name, a
  decimal number or an operator.

`#undef name` undefines a macro.

### 8.3 Conditional groups

```c
#ifdef CCISH
#  define HAVE_CCISH 1
#endif
#if N > 10
#  error "N too big"
#elif N > 5
#  define N 5
#else
#  define N 4
#endif
```

`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` and `#endif` keep or
skip groups of lines. A skipped group's lines aren't split into
tokens, so they may hold anything, but its conditional directives
still count, to find its end. Every group must end in the file it
began in.

`#if` and `#elif` take an integer expression, with its macros
expanded and `defined(name)` (or `defined name`) 1 if `name` is a
macro, else 0; any name left is 0. The expression has C's operators
but not the assignments, `++`, `--` or the comma, and `&&`, `||` and
`?:` don't evaluate what they skip (`0 && 1 / 0` is no error). An
`#elif` after a branch was kept isn't evaluated at all. One
difference from C: the arithmetic is 16-bit `int` and `unsigned` with
C's usual conversions, not the widest types, so `0x7FFF + 1 < 0` is
true here.

`#error text` stops the compile, with the text.

### 8.4 #line and the predefined macros

`#line number` makes the next line that number, and
`#line number "file"` also names the file; from there on, messages,
`__FILE__` and the line records use the new values. This is how
generated sources report their real origin.

The predefined macros are `__LINE__` (the current line's number),
`__FILE__` (the current file's name, as a string) and `__CCISH__`
(1). `-D name[=text]` and `-U name` (section 2.1) define and undefine
macros before the first line.

`#pragma plasma import MODULE` names a PLASMA module to import from
(section 11.1); any other `#pragma` is ignored. A `#` alone does
nothing. Any other
directive is an error.

## 9. What ccish doesn't (yet) do

### 9.1 Not implemented, expected soon

* `exit()`: declared in `<stdlib.h>` but not defined, so a call is an
  undefined reference when the module is linked (section 11.2).
* The rest of the C library: `printf` (which needs varargs), `malloc`,
  and the other headers (section 11.2 lists what there is).
* A struct-valued `?:` (`s = k ? a : b;`): use an `if`.
* `signed char`.

Each gives a "not supported yet" error, except `exit` and the missing
library functions (undeclared, or undefined when linked), and the
struct `?:` ("a struct can't be used as a value here").

### 9.2 Out of scope

These are deliberate limits, each a clear compile-time error:

* The types `long`, `float` and `double` (no 32-bit or floating
  arithmetic on the 6502, and no room for it in the compiler).
* `auto` (locals are automatic anyway).
* The comma operator.
* Bitfields.
* Variable argument lists (`...` in a function's parameters; the
  preprocessor's `...`/`__VA_ARGS__` is separate, section 8.2).
* `extern` with an initializer.
* C99's designated initializers (`.x = 1`, `[2] = 5`) and compound
  literals (`(struct pt){1, 2}`). Designated initializers that only go
  forward might come later (approach.md, issue 23).
* Structs over 8 bytes passed or returned by value (pass a pointer).
* `==` and `!=` on structs (as in C).
* Anything that needs more than 16 bits of address or 8 bits of
  character, which is the target's shape, not the compiler's.

We might later add some, e.g., `float`, and have them call libraries.
However, that's a "later" task.

### 9.3 Fixed limits

The limits are fixed sizes, each a `#define` beside its table, set
just above what ccish's own source needs, since every byte counts on
a 64K machine. Going past one is a clear error.

Per function:

* 1280 bytes of bytecode, 128 calls and data references, 103 labels
  and 24 `goto` labels, 31 `return`s, and 16 parameters. Each `if`,
  loop, `&&`, `||` and `?:` uses one to four labels.
* 36 `case` labels in all the `switch`es open at once, and
  parentheses, blocks and loops nested 32 deep.

Per file:

* 280 functions (declared, defined or called), 79 variables visible
  at once (globals, plus the parameters and locals in scope), 401
  parameter types in all, 40 distinct types, 6 struct tags (counting
  those in blocks that have ended), 32 struct members in all, and 203
  enum constants and typedef names visible at once.

The preprocessor: 8432 bytes of distinct names and file names, 104
macros with 704 bytes of bodies (as tokens) in all, and source lines
of 255 characters.

Other: a string literal of 255 characters (after adjacent ones are
joined); an identifier's first 31 significant characters; 32 operands
on the back end's temporaries in one function; a file name of 127
characters; and per link, 32 objects and 307 shared names.

A few other limits: a local array must fit in the 255-byte frame
with the other locals; globals are limited to 32767 bytes in the
module file (more, if the heap provides them, section 5.4); and
nothing checks evaluation-stack depth, so deeply nested calls can
overflow the VM's 32-entry stack.

## 10. How the compiler works

ccish is a pipeline of four programs, each small enough to fit in the
Apple II's memory on its own. On the host, `ccish` runs all four in
one process; on the Apple II they run one at a time, each with nearly
all of memory (approach.md, "B3" and 18.7). Each pass reads what the
previous one wrote, and nothing else, through files of a simple
documented format, so a pass can be replaced or run by itself.

The pipeline:

```
source          ccpp          cccomp         ccgen           ccrel
 .c, .h    -->  .i, .nam  -->  .ir      -->  .obj, .syms  -->  module
```

The design goal at every stage is to make the result small and fast:
each pass optimizes what it can see, and passes what it can't decide
yet to the next.

### 10.1 ccpp: the preprocessor and lexer

Reads the `.c` source and whatever it `#include`s (section 8).
Writes two files:

* `NAME.i`, the token stream: every token as a 16-bit code and value,
  in order, with a line number before each line's first token.
* `NAME.nam`, the name file: every identifier and file name as a
  byte string, with an id number in order of first appearance.
  Keywords and operators have fixed ids; a program's names get the
  rest.

The point of the name file is that no later pass has to store names:
a token's value is an id, and a pass that needs to print a name
spells it from the file as it writes it. That's important for self-hosting.
The preprocessor also joins adjacent string literals and checks the
directives' syntax.

### 10.2 cccomp: the parser

Reads the token stream and the name file. Writes the IR file
`NAME.ir`: a stream of IR ops (each an op number and one or two
16-bit operands), with the module's data and strings and a symbol
summary in the same file (OBJECT.md).

cccomp does not build an abstract syntax tree. Building an AST is the
common way to compile C, but a tree costs the memory of the whole
program at once, which a 64K machine doesn't have for a compiler.
Instead cccomp streams: it emits IR ops as it parses, and uses small
fixed structures where it has to look back:

* an expression engine (a stack of pending operators, the classic
  way to parse expressions without a tree),
* a declaration engine (C declares inside-out, `int *a[3]`, and the
  engine reverses it),
* a fixup stack of the labels made so far, for the branches that
  must jump forward,
* a short IR buffer (64 ops) that holds code just long enough to
  reorder it (a value moved ahead of an address it is stored
  through), and to flush it in the right order.

Where cccomp optimizes:

* Constant folding: arithmetic, comparisons and the other operators
  on constants are computed at compile time, with C's rules
  (16-bit wrap, truncating division, error for a constant zero
  divisor). This is needed anyway for array sizes, `case` values and
  global initializers, and it removes the constant work from the
  code.
* Dead code from constants: code a constant makes unreachable is not
  emitted (section 6.6).
* Address constants: a constant expression over addresses
  (`buf + 10`, `&m[1][2]`) is folded to one address push.
* Delayed loads: a variable's load is not emitted until the value is
  known to be needed, so `x = ...` needs no load of the old `x`.
* Loop markers: the loop's parts are written as markers
  (`LOOP`, `LOOPSTEP`, `LOOPBODY`, `LOOPEND`, IR.md) that ccgen
  turns into the fast layout (section 10.3). cccomp is the program
  that doesn't fit yet, so it only marks; the reordering goes into
  ccgen, which has room.

### 10.3 ccgen: the back end

Reads the IR file and the name file. Writes the stream files
(`NAME.obj`, the tagged stream of data, strings and functions, and
`NAME.syms`, the symbol summary; OBJECT.md).

ccgen works in stages over the IR stream:

1. **Lowering** (IR ops to machine ops). It chooses the VM's ops with
   knowledge of the types, which the IR's type-carrying ops make
   possible: the immediate forms (`ADDI 5` for `CN 5 ADD`), the fused
   memory forms (`LLW n ADD` to `ADDLW n`, the load and the add in
   one op), direct forms for locals and globals (`LLW n`, `LAW g`),
   and the absolute forms for constant addresses (reading the
   speaker is `LAB $C030`). An unsigned comparison is lowered to the
   signed form with both sides' sign bits flipped (the CMDSYS
   `__isule`-family calls are an alternative a program can use
   directly; section 12.4).
2. **Reordering.** The code for a store's value is moved ahead of the
   code for its address, so `*p++ = x` and `a[i + 1] = x` work without
   a hidden local. The loop's test and step are held and written
   after the body, so the test sits at the bottom (section 10.5).
3. **Optimization to PLASMA's forms.** A loop that can be counted
   gets PLASMA's counted-loop opcodes (section 12.2), a `switch`
   gets `SEL` and its case table, and a function with no parameters,
   locals or temporaries gets no frame at all (no `ENTER`, and
   `RET` where `LEAVE` would be).
4. **The VM peephole optimizer.** A window of VM ops is repeatedly rewritten
   to optimize it  until nothing changes. For example,
   unreachable code after a `RET` or branch is
   dropped, a store followed by a load of the same variable is one
   `DLW`, a load repeated (`x * x`) is a `DUP`, unless it fuses with
   what follows (`x + x` is `LLW ADDLW`) or is volatile, `+0` and
   friends are removed,
   a multiply by 0, 1, 3 or a power of two avoids `MUL`, and a test
   followed by a branch becomes the single branch op
   (`ISEQ BRFLS` to `BRNE`, `NOT BRFLS` to `BRTRU`). This optimization runs to
   completion across statements, with labels as the only barriers.
   Before that, in a loop's held body, a costly expression that
   doesn't change in the loop (a multiply or divide whose operands are
   constants and locals nothing in the loop changes, and no pointer
   reaches) is computed once, before the loop, into a temporary
   (section 12.4).
5. **Encoding.** One function at a time, to bytes, with fixup chains
   that fill in each branch's offset as its target is reached.

### 10.4 ccrel: the final pass and linker

Reads the stream files of one or more objects (section 2.3). Writes
the REL module, and with `-l`, a listing of it.

It lays the module out (header, the objects' data, their strings,
their code, an init function), resolves the names (a name defined in
two objects, or declared differently in two, is an error; a used name
defined in none must be a CMDSYS import), writes the module's
dependencies and its relocation table (the RLD, which tells the VM
what to fix up at load), and adds the init code that moves the
zero-filled globals to the heap when there are any
(section 5.4). The module exports nothing, and imports only CMDSYS
functions and what you declared as PLASMA imports (section 11.1), so
other C names never reach PLASMA's 16-character, case-insensitive
symbol table. A module an import comes from goes in the dependencies,
after `CMDSYS`, so the loader loads it first.

### 10.5 Where the speed comes from

The layout choices, in one place:

* Every `while` and `for` is written with its test at the bottom:
  one branch per pass, and the entry jump once
  (approach.md, "Loop design").
* A loop whose test and step have the counted shape (an `int` local
  stepped by a constant, compared signed with a constant or another
  local, in a body that calls nothing, returns nothing, holds no
  loop, and doesn't change the variable or the limit) keeps its
  count and limit on the evaluation stack, and each pass is a step
  (`ADDI`, or `INCR`) and a counted branch, `BRLT` or `BRGT`: 2 to 3
  ops of loop work per pass instead of 5 to 7 (section 12.2).
* `&&` and `||` in a condition use the VM's short-circuit branch ops
  (`BRAND`, `BROR`), which keep the left value when they decide.
* `switch` is a `SEL` and a 4-bytes-per-case table, not a compare
  chain (within its limits, section 7.5).

## 11. Calling built-in CMD library

A ccish module runs under PLASMA's command system, CMD, and can call
a small built-in library of routines that CMD exports by name; the
call is an ordinary import from CMDSYS, declared and checked as in
section 5.3. In ccish each name has `__` in front: `__puts` calls
CMD's `PUTS`. There are only a few built-ins, and they vary somewhat
depending on the platform, though many are available on all of them:
the string, heap, memory and comparison routines are the same
everywhere, and only the machine-specific ones (`__syscall`, `__call`,
`MACHID`) differ in what they do. This section is a brief summary of
the routines that can be called, as defined in the file
`src/vmsrc/apple/cmd.pla`, the Apple II's CMD.

The routines, with their ccish names, parameters and return types:

| Routine | Parameters | Returns | What it does |
|---|---|---|---|
| `__putc` | `char c` | `void` | Writes one character, uppercased on the machines that default to upper case. |
| `__putln` | none | `void` | Writes the line break (a carriage return). |
| `__puts` | `const char *s` | `void` | Writes a PLASMA string, no newline; build `s` with `pstr`. |
| `__puti` | `int n` | `void` | Writes a signed decimal integer. |
| `__putb` | `int n` | `void` | Writes a byte in hexadecimal. |
| `__puth` | `int n` | `void` | Writes a word in hexadecimal. |
| `__getc` | none | `int` | Reads one character from the keyboard. |
| `__gets` | `char p` | `char *` | Prints the prompt character, reads a line, and returns a pointer to the PLASMA string in the shared string-table buffer. |
| `__toupper` | `int c` | `int` | Uppercases a lowercase character, leaves any other character alone. |
| `__strcpy` | `char *d, const char *s` | `char *` | Copies a PLASMA string, length byte included, into `d`, and returns `d`. |
| `__strcat` | `char *d, const char *s` | `char *` | Appends the PLASMA string `s` to `d`, and returns `d`. |
| `__heapmark` | none | `void *` | Returns the current top of the heap, so a later release can discard everything since. |
| `__heapalloc` | `int n` | `void *` | Allocates `n` heap bytes, returning the address, or 0 if the heap can't hold them. |
| `__heapallocalign` | `int n, int pow2, void *freeaddr` | `void *` | Allocates `n` bytes on a `2^pow2` boundary, or 0; if `freeaddr` is not 0 it first stores the old top of the heap there. |
| `__heaprelease` | `void *p` | `int` | Puts the top of the heap back to `p`, releasing everything allocated since, and returns the bytes freed. |
| `__heapavail` | none | `int` | Returns the number of heap bytes still free. |
| `__memset` | `void *p, int c, int n` | `void` | Fills `n` bytes at `p` with the 16-bit pattern `c`. |
| `__memcpy` | `void *d, const void *s, int n` | `void` | Copies `n` bytes from `s` to `d`, correct even when they overlap on an Apple II (plvm's calls the host's `memcpy`, which needn't be). |
| `__isugt` | `int a, int b` | `int` | 0 or -1: is `a` greater than `b` as `unsigned`? |
| `__isuge` | `int a, int b` | `int` | 0 or -1: is `a` at least `b` as `unsigned`? |
| `__isult` | `int a, int b` | `int` | 0 or -1: is `a` less than `b` as `unsigned`? |
| `__isule` | `int a, int b` | `int` | 0 or -1: is `a` at most `b` as `unsigned`? |
| `__syscall` | `int cmd, void *params` | `int` | Runs a ProDOS system call with the command byte `cmd` and the parameter block at `params`, returning the error code (0 on success). |
| `__call` | `int addr, int a, int x, int y, int st` | `int` | Calls a 6502 routine at `addr` with `A`, `X`, `Y` and the status set, and returns a pointer to a four-byte block with the result in `A`, `X`, `Y` and the status. plvm doesn't implement it ("SYSCALL6502 unimplemented!"); there, call the address through a function pointer instead, which runs it as 6502 code. |

The export table also holds `DIVMOD(a, b)` which divides `a` by `b` and returns two values, the
quotient and the remainder. You'll need to assign that return value to a
struct with the same shape (section 1.3).

### 11.1 Calling other PLASMA modules

A module can also use what other PLASMA modules export: functions,
data, and tables of functions such as FILEIO's and CONIO's. You
declare each as an ordinary C `extern` (or function prototype), and
mark it as a PLASMA import in one of two ways:

```c
#pragma plasma import FILEIO        /* load FILEIO first */
struct t_fileio {
  int (*getpfx)(char *);
  /* ... */
  int (*open)(char *);              /* PLASMA's open(path), a PLASMA string */
  int (*close)(int);
  int (*read)(int, char *, int);
};
extern struct t_fileio fileio;      /* the PLASMA symbol FILEIO */

extern struct t_cmdsys cmdsys __asm__("CMDSYS");   /* named by a label */

ref = fileio.open(pstr("MYFILE"));  /* a load from the table, then ICAL */
```

* **An asm label**, `__asm__("NAME")` after the declarator (the gcc
  and clang extension for the same job), gives the PLASMA symbol's
  name. Without one, the symbol is the C name, uppercased; PLASMA keeps
  16 significant characters.
* **`#pragma plasma import MODULE`** makes every function and `extern`
  declared after it, in the same file, an import from MODULE, which the
  loader loads first. It covers the rest of its own file only: a
  header's pragma doesn't make the externs of the file that includes
  it imports. A later pragma in the file replaces it. A CMDSYS function
  (`__puts`) declared after one is still CMDSYS's.
* A declaration with a label but no pragma imports from a module
  that's already loaded, such as CMDSYS's own data (`cmdsys`, whose
  `syserr` byte is the error code of FILEIO's calls).
* It's an import only if no object linked into the module defines it,
  and only if something uses it; a module goes into the module's
  dependencies only if one of its imports is used. Any other `extern`
  that's used and defined nowhere is still an "undefined reference",
  so a misspelled name is still caught when the module is linked.
* PLASMA calls check nothing: you must pass exactly the arguments the
  routine takes, and declare the return type it gives (`void` for none).
  A PLASMA string is a length byte, then the characters: pass
  `pstr("...")` for a literal. A routine that returns two values
  (`geteof`, `getmark`) returns a struct of two `int`s.
* These declarations belong in ccish's own headers, under
  `#ifdef __CCISH__` when a header is shared with another compiler.
  Under `plvm`, FILEIO and CONIO are built in, so their dependencies
  load no file.
* `<plasma.h>` and `<fileio.h>`, in the library's headers (section
  11.2), declare `cmdsys` and `fileio` this way, with their tables'
  layouts.

### 11.2 The C library

ccish has a small C library, written in ccish's C: the headers in
`lib/include/`, and one source per function in `lib/`. `make`
compiles each into `tests/out/lib/NAME` (`NAME.obj` and `NAME.syms`).
ccrel links whole objects, and doesn't search a library yet, so you
name on the link line the objects your program calls, and the ones
they call:

```
$ L=tests/out/lib
$ ./ccish -o copy copy.c $L/start $L/fopen $L/path $L/fread $L/fwrite $L/strlen
```

| Header | What's there |
|---|---|
| `<string.h>` | `strlen`, `strcmp`, `strcpy`, `strcat`, `strncpy`, `strncat`, `strchr`, `strrchr`, `memcpy`, `memmove`, `memset` |
| `<stdio.h>` | `fopen`, `fclose`, `getc`, `putc`, `fputs`, `fread`, `fwrite`, `remove`, and `fflush` (a macro), `stdin`, `stdout`, `stderr`, `EOF` |
| `<stdlib.h>` | `EXIT_SUCCESS`, `EXIT_FAILURE`, and `exit` (declared, but not defined yet) |
| `<stddef.h>` | `size_t` (`unsigned`), `ptrdiff_t`, `NULL` |
| `<plasma.h>` | `cmdsys`, CMDSYS's table (section 11.1); not standard C |
| `<fileio.h>` | `fileio`, FILEIO's table (section 11.1), and `<plasma.h>`; not standard C |

* **Which objects:** each is named for its function, so `strlen`'s is
  `strlen`. Some call others: `fopen` (which holds `fclose` too) and
  `remove` call `path`, and `path` and `fputs` call `strlen`. `start`
  is the startup for `main(argc, argv)` (section 5.1). A missing one
  is an "undefined reference" that names the function.
* **stdio is unbuffered**, over FILEIO: each call is one FILEIO call,
  so read and write in blocks, not a byte at a time. There's nothing
  to flush, so `fflush` is 0.
* **A `FILE *` is FILEIO's reference number**, or negative for the
  console, so there's no table of streams and no limit but FILEIO's
  (ProDOS's 8 open files). Don't look inside one. `stdin`, `stdout`
  and `stderr` are all the console; a `'\n'` written to it is CMDSYS's
  newline, so it works on every machine.
* **`fopen`** takes a mode starting with `r` (read) or `w` (write,
  after destroying the file and creating it as a binary file, ProDOS
  type $06); FILEIO opens a file for both either way. Any other mode,
  or a path over 127 characters, gives `NULL`. Files are binary: no
  newline conversion.
* **`remove`** can't report a missing file under plvm, whose FILEIO
  destroy returns a stale error code; it returns 0, or `EOF` for a path
  that's too long. `fclose` returns 0.
* **`memcpy` and `memset`** call CMDSYS's `__memcpy` and `__memset`.
  `memmove` copies a byte at a time, since plvm's `__memcpy` calls the
  host's `memcpy`, which needn't handle an overlap.
* **`exit` isn't implemented.** It has to unwind the VM back to CMD
  (the 6502 stack and the VM's frame and evaluation stacks) and close
  the open files, which takes PLASMA's except/throw or a 6502 routine
  of ccish's own (doc/bootstrap-test.md, R8). So a call to it is an
  undefined reference when the module is linked. ccish's own passes
  call it on their first error, and link `tools/plvmexit.c`'s
  stand-in, for plvm only: it prints `tools/ccstat.pla`'s status
  marker and runs a 6502 BRK, which makes plvm exit. Don't use it on
  an Apple II, where a BRK drops into the monitor.

## 12. Writing small and fast code

Other than CMD routines like `__puts` requiring `pstr(...)`,
and various limits, this should "feel" like a normal C implementation,
and most C optimizations apply. Here are some special tips for ccish.

### 12.1 Seeing the code you get

The compiler's output is short enough to read, and the flags make it
easy:

* `ccish -l program.c` prints a listing of the generated bytecode,
  one op per line: the address, the hex bytes, the mnemonic, and a
  comment for data and imports. From `tests/run/loops.c`:

  ```
  main:
    1047  58 10 00 ENTER 16,0
    104A  02       CN 1
    104B  76 08    SLW 8
    ...
    106B  66 02    LLW 2
    106D  06       CN 3
    106E  44       ISGT
    106F  4E 11 00 BRTRU +17  ; to 1081
  ```

* `ccish -d program.c` prints the machine ops after the peephole
  stage, that is the final code, with the labels:

  ```
  ; total
      ENTER 6 1
      CN 0
      SLW 4
      ...
      DLW 2
      LLW 4
      ADDLW 2
      SLW 4
      INCR
  ; label 1
      BRLT 0
      DROP
      DROP
      LLW 4
      LEAVE 6
  ```

  (the files in `tests/gen/`, each with its `.exp`, are this output
  for small programs, and the tests check it op by op).
* `ccish -i program.c` prints the IR, as the back end reads it, with
  each function's source line.
* `ccish -k` keeps the intermediate files (`.i`, `.nam`, `.ir`,
  `.obj`, `.syms`) so you can inspect any stage.
* To count what the code does, run it traced: `plvm -t program`
  prints every 6502 instruction, so the number of traced lines is the
  number of instructions the program ran.

The op-by-op costs are in [../doc/Bytecodes.md](../../doc/Bytecodes.md)
and the cycle analyses in
[../doc/Bytecodes-cycles.md](../../doc/Bytecodes-cycles.md); the
machine's shape (16-bit words, `zp,X` stack ops, the eval stack) is
where the advice below comes from.

### 12.2 The counted loop

Loops are where programs spend their time, and ccish has a special
optimization for the common count-up-and-down loop. When a loop's
test and step have the counted shape, ccgen keeps the count and the
limit on the evaluation stack for the life of the loop; each pass is
a step and a counted branch, with the index refreshed in the body
(the loop's exit, including a `break`, drops the two stack values).
The shape:

* `for (i = a; i < b; i++)` and the same with `<=`, `>` or `>=`, and
  the step `i--`, `i += k` or `i -= k` (a constant `k`),
* `i` an `int` local, compared signed, or a `char` local whose count
  stays in 0 to 255 to the end (below),
* the limit a constant or another local (not a global, and not a
  pointer),
* a body that makes no calls, has no `return`, holds no inner loop,
  and changes neither `i` nor the limit, and takes neither's address.

A `char` counter wraps at 256, where the count on the stack doesn't,
so a `char` loop is counted only when its count can't pass 255 or go
below 0, the last step included: a constant limit with room for the
last step (`c < 200`, `c <= 254`, `c > 0`, `c >= 1`, `c < 250` by 5),
or a `char` local limit with `<` or `>` by 1. `for (c = 0; c <= 255;
c++)` never ends in C, so it isn't counted, nor is a loop against an
`int` limit. Its count is stored with `DLB`.

`while` loops can't be counted (a counted loop needs a step to
check).
A `goto` loop is never counted. When a loop can't be counted, the
test-at-the-bottom layout still applies (section 10.5).

What it costs per pass, for `for (i = 0; i < n; i++)`:

| The loop | `i < n` | `i != n` |
|---|---|---|
| can be counted | 3 (2 with a constant limit) | 5 |
| can't be counted | 6 | 5 |

So, to get it: write the loop in the counted shape, with an `int`
counter, a constant or local limit, a signed `<` (or `<=`, `>`,
`>=`) test, and a body with no calls. A limit in a global stops the
counting (a store through a pointer might change it); copy it to a
local first in a hot loop. `!=` is never rewritten to `<` (they
differ for `unsigned` and pointers), and for a loop that can't be
counted, `!=` is the cheaper test, since it is one op, `BRNE`, while
`<` is a compare and a branch.

ccgen does that last rewrite for you where it can prove it's the same:
a loop that can't be counted (say, it calls something), with the
counted shape, a step of 1 or -1, a constant limit, and an init of a
constant that isn't past the stop, gets `!=` at its stop:
`for (i = 0; i <= 9; i++) f(i);` ends `CN 10 BRNE`. Only for an
outermost loop, with no `goto` label before it or in it, whose body
doesn't store `i`, and whose `i` has never had its address taken: then
nothing else can change `i`. In a nested loop, code after it in the
outer loop could take `i`'s address before it runs again, so a loop in
another loop, or after a `goto` label (a `goto` back makes a loop too),
keeps its `<`; and for the same reason such a loop isn't counted if
its body stores through a pointer. Unless its variable, and any local
its limit reads, are `register` (section 5.2): no pointer can reach
those, so then a nested loop is counted, or ends at `!=`, all the
same.

### 12.3 Writing small code

* **Constants do the work at compile time.** Integer operations on
  constants, and reads of `const` variables with constant
  initializers, are folded (section 6.6), so write the constants
  plainly and let the compiler remove them. Address constants fold to
  one push (section 5.6).
* **Use `char` for bytes.** A `char` is a byte, and the byte ops
  (`LLB`, `ADDLB`, ...) are smaller and faster than their word
  forms; there is no penalty for it being unsigned (section 4.2).
* **Big buffers: `static` locals or globals.** A local array fits in
  the 255-byte frame; a `static char line[300];` takes no frame
  space, and a global over 256 bytes without an initializer is not
  in the module file at all (section 5.4).
* **Small structs by value, big ones by pointer.** A struct of up to
  8 bytes moves as a word or two; bigger, every copy is a `__memcpy`
  call, and passing or returning one by value isn't allowed
  (section 4.7).
* **Pointers compare with `==` and `!=`.** An ordering comparison of
  pointers adds a sign-bit flip (`CW $8000 XOR`) to each side
  (section 12.4); equality adds nothing.
* **Let the peephole work.** Store then load the same variable
  (becomes `DLW`), add or index a local by a constant (becomes the
  fused `ADDLW`/`IDXLB` and friends), and test then branch (becomes
  one op), are all automatic; the patterns to avoid are the ones the
  optimizer can't see across, like a `volatile` in the way.
* **The helpers come once.** The unsigned `/`, `%` and `>>` helpers
  are added to the module once, however many files call them
  (section 4.3).
* **Optimizations are per-function.** There's no iterprocedural analysis.
  Each function is optimized, but nothing between functions, so
  tight loops should be inside a single function and not call out.
  Use `#define` if you want to inline code.

### 12.4 Writing fast code

The 6502 VM's ops are where the speed goes. The working rules, from
approach.md section 20 (each measured against the VM's code):

* **Prefer unsigned `char` and signed `int`.** Byte loads
  zero-extend, so an unsigned `char` costs nothing; and the VM's
  ordering compares, `DIV`, `MOD` and `SHR` are signed, so a signed
  `int` is free, while an `unsigned` one costs extra only for
  `< > <= >=`, `/`, `%` and `>>`. `+ - *`, `==` and `!=` work the
  same for both. (The CMDSYS `__isult`/`__isuge`/`__isugt`/`__isule`
  functions, section 5.3, do an unsigned compare in one call; a
  program can call them directly wherever the inline sign-bit flip
  would be more code, as in a betweenness test; the analysis is in
  `doc/betweenness.md`.)
* **Test pointers with `==` and `!=`, not `< > <= >=`**
  (section 12.3). `while (p != end)`, not `while (p < end)`.
* **A `char` index is a little faster than an `int` index.**
  Reading a byte local skips the high byte (a byte load is about 5
  cycles faster than a word load, and the fused byte adds about 12
  faster). But a `char` index limits an array to 256 elements and
  wraps: `for (char i = 0; i < 256; i++)` never ends.
* **For a simple scan, walk a pointer; otherwise, consider an
  index.** Reading through a pointer is two ops (`*p`), and
  `buf[i]` is three (with the fused op). An index wins when the loop
  counter is needed anyway (one variable instead of two), when
  several arrays share it, when the loop test can be `i < n` (a
  plain signed compare, and a counted loop) instead of `p < end`,
  and for random access.
* **Multiplying by a constant** is cheap for 0, 1, 2, 3 and powers
  of two (shifts and a doubled add); other constants use the VM's
  `MUL`, a 16-step loop. Dividing by a constant uses `DIV`.
* **Declare hot loops' variables `register`** (section 5.2). ccgen
  then knows no pointer reaches them, so a loop in another loop can be
  counted, and an expensive expression of them that doesn't change in
  the loop (`i * w / 12` in a loop over `j`) is computed once, before
  it, not on every pass. Without `register`, it does both only for an
  outermost loop whose variables' addresses are never taken. Hoisting
  takes a multiply (but not by 0 to 3 or a power of 2, which is cheap
  anyway) or a divide by a constant, from the innermost loop, if its
  body fits ccgen's 128-op loop buffer: up to two values a loop, each
  in a temporary of ccgen's own (2 frame bytes in the function).
* **Pick the loop's test with care** (section 12.2): an `int`
  counter in a loop that can be counted takes `<` with a constant or
  local limit, with calls kept out of the body; otherwise, and for
  pointers and `unsigned` counters, `!=`.

The cycle cost of each op, with the 6502 line-by-line behind each
number, is in [../doc/Bytecodes-cycles.md](../../doc/Bytecodes-cycles.md);
when in doubt, compile it both ways and read the listing.

### 12.5 Optimizations you can count on

Here is a short list of some of the optimizations you can count on:

* **Constant op constant.** Put constants next to each other in operations.
  They'll be computing during compilation, not at runtime.
* **A self tail call is a jump, not a call.** A function's own tail
  call, whose result is the function's result (`return f(...);` in
  `f`, or a final `f(...);` in a `void` function), compiles to the
  arguments' stores into the function's own parameter slots, then a
  jump back to the top of the function, instead of a call. The jump
  reuses the current frame, so a tail recursion runs at any depth,
  like a loop: the base case's return is the function's return. It
  fires wherever the tail position appears (the end of the function,
  an `if` arm, a loop body), and the arguments may be any type.
  Anything else stays an ordinary call with a frame per level
  (section 5.2): a call whose result is used (`return n * f(n - 1);`),
  a call that is not the function's final action, and calls to
  another function. (approach.md, decision 33; `tests/run/taildeep.c`
  runs the tail form at depth 5000, and `tests/gen/gentail.c` pins
  the form and the non-forms.)
* **A "between" test of form (x >= c1 && x <= c2)** where c1 and c2
  are constants. Where x is a `char` and the range is a power-of-two
  block of bytes (its width, c2 - c1 + 1, is a power of two, and the
  range lies within 0 to 255), or where x is an `int` and the range is
  a power-of-two block of words (the width a power of two, and the
  range not empty), ccish rewrites it to one load, the misalignment r
  subtracted, the block's mask ANDed, and one compare to the aligned
  base, smaller and faster than the two comparisons: an aligned range
  (r of 0) emits no subtract, an r of 1 is a `DECR`, and the
  comparison meets its branch as a `BRNE` in the peephole stage. A
  `(char)` cast on an `int` ((char)x >= c1 && (char)x <= c2) takes
  the byte form on the word load, with the cast's own mask folded
  into the block's. A range that is neither a block of bytes nor a
  block of words keeps the two comparisons. (The analysis is in
  `doc/betweenness.md`; `tests/gen/genbetween.c` pins the forms and
  the non-forms, and `tests/xrun/xbetween.c` cross-checks every case
  against the host compiler over the whole 16-bit space.)

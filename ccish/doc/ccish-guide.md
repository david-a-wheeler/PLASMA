# The ccish guide

A user guide to ccish, the C compiler for the PLASMA virtual machine.

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

## 1. What ccish is

ccish compiles a dialect of C into PLASMA REL modules that run on the
6502, on any of PLASMA's targets (the Apple II through the PLEIDES VM,
and the C VM `plvm` used for testing). The dialect and the compiler
share the name: a program written in it is *C-ish*, and `ccish` is the
compiler. It is intended to aggressively optimize its generated results.

The ccish compiler can run on an Apple II, and can self-host,
if the loading system can stream RLD values (work in progress).

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
  with all the checks, and the small build for the 64K machine. The
  small build writes exactly the same objects and modules.

### 1.3 Where it fits

ccish's output is a PLASMA module. It runs under `plvm` on the host
for testing, and under PLASMA's 6502 VMs on the Apple II, Apple 1,
C64 and the JIT target. A ccish program may use CMDSYS functions
(`puts`, `putc`, `puti`, `heapalloc`, and the rest, section 5.3), and
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

* It doesn't have C's standard library, but the PLASMA CMD library for its platform
  as noted below.
* If you pass strings to CMD routines like `puts`,
  you must send *PLASMA* strings. The special call
  `pstr(...)` converts constant C strings to PLASMA strings.
  This means you must say `puts(pstr("Hello"));` and not `puts("Hello");`.
  Omitting the `pstr()` will typically print garbage.
* The `int` type is a signed 16-bit that always wraps.

## 2. Using the compiler

### 2.1 Compiling a file

```
ccish [-l] [-d] [-i] [-k] [-o output] [-I dir] [-D name[=text]] [-U name] input.c
```

* `-o output`: the module file to write. The default is the input name
  without `.c`. To run a module with `plvm`, give it a lowercase name
  with no extension, and run `plvm name` from its directory.
* `-l`: print a listing of the generated bytecode (full build only).
* `-d`: print the machine ops after the peephole stage, that is the
  final code (the code-generation tests compare against this).
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

Example:

```
$ cat hello.c
int add(int a, int b) { return a + b; }
int main(void) { puts(pstr("Hello")); puti(add(2, 3)); putln(); return 0; }
$ ./ccish hello.c && ../src/plvm hello
Hello5
```

`puts` and the rest of the CMDSYS functions are declared already
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
ccrel [-l] [-o module] output...
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
* A function that no object defines must be a CMDSYS function, which
  is imported (section 5.3); a program may define its own function of
  a CMDSYS name, with the same type, and calls then go to it.
  Anything else used and not defined is an "undefined reference".
* `main` (not `static`) in any object is the entry point.
* The module exports nothing, and imports only CMDSYS functions, so C
  names are matched in full, with their case, and never reach
  PLASMA's case-insensitive, 16-character symbol table.

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
`-DCCISH_SMALL`: it leaves out `-l`, and checks that only a compiler
bug or a damaged file would fail (so far `ccgen`'s check of the
blocks it moves; later, diagnostics that a valid program never
triggers). `ccgen -i` is in both. So the small build should compile
only code the full build (or gcc) has already accepted. It writes
exactly the same objects and modules; the tests check that.

## 3. Lexical conventions

### 3.1 Identifiers

An identifier is a letter or underscore, followed by letters, digits
and underscores. Only the first 31 characters are significant; the
rest are ignored. A keyword is not an identifier.

The supported keywords are:

```
break    const    continue default do     else     enum   extern
for      goto     if       int    return   sizeof   short    static
struct   switch   typedef  unsigned void     volatile while
```

plus `signed`, as in `signed int`, which is `int`, and `short`, a
synonym for `int` (section 4.1). Six more of C89's keywords are
recognized so the error is clear, but not supported (section 9.1):
`auto`, `double`, `float`, `long`, `register` and `union`.

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
as `puts` expect. `pstr()` accepts only a string literal, and its
type is `char *` too.

## 4. Types and values

The arithmetic types are `int` (16 bits), `char` (8 bits, unsigned)
and `unsigned` (16 bits); `void` names the absence of a value.
Pointers are 16 bits. `short` is a synonym for `int` (section 4.1);
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
`memcpy(dst, src, n)`: CMDSYS's, or the program's own if it declares
one, which must take C's arguments. A struct with a `const` member
can't be assigned as a whole. `==` on structs is an error, as in C.

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
array sizes and qualifiers. Typedefs of function types are not
supported. Typedef names have block scope, as in C.

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
  can't size an array or initialize a global, and a `const int`
  holding 0 isn't a null pointer.
* `volatile` changes no code: ccish never drops or merges memory
  reads. It is checked as part of the type. Its use is the I/O
  registers (section 4.5).

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
* `main` takes no parameters (`int main(void)`), and its return value
  is ignored.
* A struct parameter or return value must be at most 8 bytes
  (section 4.7). Passing a struct needs a prototype.
* A function can call itself.
* A function that is called but not defined in any file linked into
  the module must be a CMDSYS function (section 5.3).

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

### 5.3 Calling CMDSYS

A function that is called but not defined in any file linked into the
module must be a CMDSYS function; it is imported from CMDSYS, and a
declaration must match its exact types (the compiler checks calls
against them, so a mistake is caught at compile time and not at load
time). The CMDSYS functions are declared already: a call with no
declaration is auto-declared from the table in `src/parse.c`
(`cmdsys_sigs[]`), or you may write the declaration yourself, as the
test header `inc/ptest.h` does:

```c
extern void puti(int);
extern void putc(char);
extern void putln(void);
extern void puts(const char *);
```

The common ones, with their C types:

```c
void putc(char c);        /* write one byte */
void putln(void);         /* the line break */
void puts(const char *s); /* write a PLASMA string */
void puti(int n);         /* write a signed decimal integer */
void putb(int n);         /* binary */
void puth(int n);         /* hex */
int  getc(void);          /* read a byte */
int  heapavail(void);     /* the heap's free bytes */
void *heapalloc(int n);   /* allocate n bytes */
int  heaprelease(void *p);
void *heapmark(void);
void memset(void *p, int c, int n);  /* fill with a 16-bit pattern */
void memcpy(void *d, const void *s, int n);
char *strcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
int  toupper(int c);
int  sext(int v);         /* sign-extend the low byte */
int  isult(int a, int b); /* unsigned a < b, as -1 or 0 */
int  isuge(int a, int b);
int  isugt(int a, int b);
int  isule(int a, int b);
```

The full list and types are `cmdsys_sigs[]` in `src/parse.c`.

These are PLASMA's functions with PLASMA's meanings, not C's. For
example, `puts` takes a PLASMA string (length-prefixed, so build one
with `pstr`) and adds no newline, and `memset` fills with a 16-bit
pattern, so `memset(p, 'a', 4)` gives `a`, NUL, `a`, NUL (with 0, as
ccish uses it, that makes no difference). A pointer that a function
only reads is `const` (`puts`, the source of `strcpy`, `strcat` and
`memcpy`), so passing a `const char *` needs no cast.

A program may define its own function of a CMDSYS name, with the same
type, and calls then go to it instead of the import.

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
an initializer, and `extern` in a block, aren't supported. `sizeof`
an array of unknown size is an error.

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
  `"abc" + 1`, `(char *)0xC030`). The expression parser folds these
  in code too, so `buf + 10` is one address push. Casting an address
  to an integer (`(int)&x`) gives "initializer is not a constant
  ccish supports".
* Arrays: a brace list, one level deep (`int t[5] = {1, 2, 3};`),
  with the rest zero and an optional trailing comma. The size may come
  from the list (`int t[] = {1, 2};`). A two-dimensional array takes
  a flat list (`int m[2][3] = {1, 2, 3, 4, 5, 6};`); nested braces
  aren't supported yet. A `char` array may be a string
  (`char s[] = "hi";` has 3 elements), and so may each row of a
  `char` array of arrays (`char kw[][6] = {"int", "char"};`).
* Structs: a flat brace list over the struct's scalar members in
  order (`struct pt p = {1, 2};`), and the same for arrays of structs
  and structs holding arrays or structs, all as one flat list
  (`struct pt ps[] = {1, 2, 3, 4};` is 2 points). A `char` array
  member may be a string (`{"nut", 40}`).
* A scalar may be in braces (`int x = {5};`).

Local initializers may be any expression, and run when the
declaration is reached.

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
putc('a');
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
  puti(sum);
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
#include "file.h"     /* beside the including file, then the -I dirs */
#include <file>       /* only the -I dirs */
```

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

`#pragma` is ignored, for now. A `#` alone does nothing. Any other
directive is an error.

## 9. What ccish doesn't (yet) do

### 9.1 Not implemented, expected soon

* `union`
* `signed char`

Each gives a "not supported yet" error that names the token.

### 9.2 Out of scope

These are deliberate limits, each a clear compile-time error:

* The types `long`, `float` and `double` (no 32-bit or floating
  arithmetic on the 6502, and no room for it in the compiler).
* `auto` and `register` (locals are automatic anyway; there is no
  register storage to name).
* Function pointers, and any declarator in parentheses
  (`int (*p)[4]`).
* The comma operator.
* Bitfields.
* Variable argument lists (`...` in a function's parameters; the
  preprocessor's `...`/`__VA_ARGS__` is separate, section 8.2).
* Local function declarations (declare functions at file scope).
* `main` with parameters, `extern` with an initializer, and
  `extern` in a block.
* Nested braces in initializers (`int m[2][3] = {{1,2,3},{4,5,6}};`
  takes a flat list, section 5.6).
* Typedefs of function types.
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

* 1280 bytes of bytecode, 128 calls and data references, 104 labels,
  and 31 `return`s. Each `if`, loop, `&&`, `||` and `?:` uses one to
  four labels.

Per file:

* 280 functions (declared, defined or called), 79 variables visible
  at once (globals, plus the parameters and locals in scope), 401
  parameter types in all, 40 distinct types, 6 struct tags (counting
  those in blocks that have ended), 32 struct members in all, and 203
  enum constants and typedef names visible at once.

The preprocessor: 8432 bytes of distinct names and file names, 72
macros, and source lines of 255 characters.

Other: a string literal of 255 characters (after adjacent ones are
joined); an identifier's first 31 significant characters; 32 operands
on the back end's temporaries in one function; a file name of 127
characters; and per link, 32 objects and 163 shared names.

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
   `isule`-family calls are an alternative a program can use
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
   `DLW`, a duplicate load is a `DUP`, `+0` and friends are removed,
   a multiply by 0, 1, 3 or a power of two avoids `MUL`, and a test
   followed by a branch becomes the single branch op
   (`ISEQ BRFLS` to `BRNE`, `NOT BRFLS` to `BRTRU`). This optimization runs to
   completion across statements, with labels as the only barriers.
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
(section 5.4). The module exports nothing and imports only CMDSYS
functions, so C names never reach PLASMA's 16-character,
case-insensitive symbol table.

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
section 5.3. There are only a few built-ins, and they vary somewhat
depending on the platform, though many are available on all of them:
the string, heap, memory and comparison routines are the same
everywhere, and only the machine-specific ones (`syscall`, `call`,
`MACHID`) differ in what they do. This section is a brief summary of
the routines that can be called, as defined in the file
`src/vmsrc/apple/cmd.pla`, the Apple II's CMD.

The routines, with their ccish names, parameters and return types:

| Routine | Parameters | Returns | What it does |
|---|---|---|---|
| `putc` | `char c` | `void` | Writes one character, uppercased on the machines that default to upper case. |
| `putln` | none | `void` | Writes the line break (a carriage return). |
| `puts` | `const char *s` | `void` | Writes a PLASMA string, no newline; build `s` with `pstr`. |
| `puti` | `int n` | `void` | Writes a signed decimal integer. |
| `putb` | `int n` | `void` | Writes a byte in hexadecimal. |
| `puth` | `int n` | `void` | Writes a word in hexadecimal. |
| `getc` | none | `int` | Reads one character from the keyboard. |
| `gets` | `char p` | `char *` | Prints the prompt character, reads a line, and returns a pointer to the PLASMA string in the shared string-table buffer. |
| `toupper` | `int c` | `int` | Uppercases a lowercase character, leaves any other character alone. |
| `strcpy` | `char *d, const char *s` | `char *` | Copies a PLASMA string, length byte included, into `d`, and returns `d`. |
| `strcat` | `char *d, const char *s` | `char *` | Appends the PLASMA string `s` to `d`, and returns `d`. |
| `heapmark` | none | `void *` | Returns the current top of the heap, so a later release can discard everything since. |
| `heapalloc` | `int n` | `void *` | Allocates `n` heap bytes, returning the address, or 0 if the heap can't hold them. |
| `heapallocalign` | `int n, int pow2, void *freeaddr` | `void *` | Allocates `n` bytes on a `2^pow2` boundary, or 0; if `freeaddr` is not 0 it first stores the old top of the heap there. |
| `heaprelease` | `void *p` | `int` | Puts the top of the heap back to `p`, releasing everything allocated since, and returns the bytes freed. |
| `heapavail` | none | `int` | Returns the number of heap bytes still free. |
| `memset` | `void *p, int c, int n` | `void` | Fills `n` bytes at `p` with the 16-bit pattern `c`. |
| `memcpy` | `void *d, const void *s, int n` | `void` | Copies `n` bytes from `s` to `d`, correct even when they overlap. |
| `isugt` | `int a, int b` | `int` | 0 or -1: is `a` greater than `b` as `unsigned`? |
| `isuge` | `int a, int b` | `int` | 0 or -1: is `a` at least `b` as `unsigned`? |
| `isult` | `int a, int b` | `int` | 0 or -1: is `a` less than `b` as `unsigned`? |
| `isule` | `int a, int b` | `int` | 0 or -1: is `a` at most `b` as `unsigned`? |
| `syscall` | `int cmd, void *params` | `int` | Runs a ProDOS system call with the command byte `cmd` and the parameter block at `params`, returning the error code (0 on success). |
| `call` | `int addr, int a, int x, int y, int st` | `int` | Calls a 6502 routine at `addr` with `A`, `X`, `Y` and the status set, and returns a pointer to a four-byte block with the result in `A`, `X`, `Y` and the status. |

The export table also holds `DIVMOD(a, b)` which divides `a` by `b` and returns two values, the
quotient and the remainder. You'll need to assign that return value to a
struct with the same shape (section 1.3).

## 12. Writing small and fast code

Other than CMD routines like `puts` requiring `pstr(...)`,
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
* `i` an `int` local, compared signed,
* the limit a constant or another local (not a global, and not a
  pointer),
* a body that makes no calls, has no `return`, holds no inner loop,
  and changes neither `i` nor the limit, and takes neither's address.

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
  8 bytes moves as a word or two; bigger, every copy is a `memcpy`
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
  same for both. (The CMDSYS `isult`/`isuge`/`isugt`/`isule`
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

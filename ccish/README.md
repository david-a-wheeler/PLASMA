# ccish: a C-ish compiler for the PLASMA VM

`ccish` compiles a subset/superset of C into PLASMA to run on 6502-based systems.
It heavily optimizes its results and is self-hosting.

CCish is dedicated to the memory of Bob Pew.
It was developed with the assistance of AI, so if that bothers you, don't use it.
Everyone else, enjoy.

## What it is

ccish compiles C into PLASMA REL modules, which run on PLASMA's VMs:
the Apple II and its other 6502 targets, and the C VM `plvm` on the
host. It's a subset/superset of C by design: what it supports generally means
what C says, so the same source compiles with gcc or clang, but ccish leaves
out what's costly on a 64K machine and not needed to compile itself, such
as floating point and `long`. Using one of those is a compile-time
error.

The ccish compiler is divided into four parts:

* `ccpp` (the preprocessor and lexer) reads the source; it writes tokens
  and a name file.
* `cccomp` (the parser) reads the tokens and writes an IR file.
* `ccgen` (the code generator) reads the IR and name file
  and writes object files (both are removed unless `-k`).
* `ccrel` (the final pass) links 1+ objects into a REL module.

The ccish compiler aggressively optimizes for its target (given its
limitations) including constant folding, self tail call optimization,
range subset sometimes (becoming AND), hoisting some expensive operations
from within loops, compile-time order swapping, and so on.
It does this through parser constant folding, IR window optimization,
and a VM peephole optimizer that implements many optimization patterns.
Its results are nowhere near as fast as assembly, but it will generally be
much smaller and "fast enough" for many tasks.

ccish compiles itself, and `make bootstrap` checks that it reproduces
itself byte for byte under plvm. Programs it compiles run on an
emulated 64K Apple IIe (see `samples/`), and so does the compiler
itself: its four passes, built as images (`-x`) and run through XLOAD,
ccish's own second-stage loader ([doc/xload.md](doc/xload.md)), compile
a program on an emulated 64K Apple IIe. (Its heart, a front-end parser
named `cccomp`, is too big for PLASMA's own loader there.) `exit()`
isn't implemented on the Apple yet.

## Using it

Build the compiler and run its tests:

```
make
```

Then compile and run a program:

```
ccish [-l] [-d] [-i] [-k] [-o output] [-I dir] [-D name[=text]] [-U name] input.c [object...]
```

```
$ cat hello.c
int add(int a, int b) { return a + b; }
int main(void) { __puts(pstr("Hello")); __puti(add(2, 3)); __putln(); return 0; }
$ ./ccish hello.c && ../src/plvm hello
Hello5
```

`__puts` is PLASMA's CMDSYS `puts`, which takes a PLASMA
(length-prefixed) string, so `pstr()` turns the C string literal into
one. `-o` names the module (by default, the input's name without
`.c`), `-l` prints a listing of the bytecode, and the `object...`
arguments are more objects to link, such as the C library's. On the
Apple II, the four passes (`ccpp`, `cccomp`, `ccgen` and `ccrel`) run
as separate programs.

## More information

* [doc/ccish-guide.md](doc/ccish-guide.md): the user guide. It covers
  the options, the passes, linking, the C library, the language and
  how it differs from C, the limits, how the compiler works, and how to
  write small, fast code.
* [approach.md](approach.md): the design, every decision and why.
* [CCPP.md](CCPP.md), [OBJECT.md](OBJECT.md) and [IR.md](IR.md): the
  intermediate file formats and the IR's ops.
* [../doc/Bytecodes.md](../doc/Bytecodes.md): the PLASMA VM.

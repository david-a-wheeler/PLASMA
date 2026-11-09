# Plan: `_Static_assert` (C11 6.7p6)

We want `_Static_assert ( constant-expression , string-literal ) ;`
so that we can state the invariants that careful constant assignments
rely on (token values, IR values) right where the code depends on
them, mostly in the .h files. A false or non-constant assertion stops
the build with the file and line; a true one generates nothing: no IR
ops, no data, no work for ccgen or ccrel. The message string takes no
space either.

`#if ... #error` already checks invariants among `#define` values at
no cost, but the tokens (tokin.h) and IR ops (ir.h) are enums, which
`#if` can't see, so we need `_Static_assert`.

(First written 2026-11-08 by another agent; revised 2026-11-09 after
review against the sources, which by then used `//` comments.)

## Summary

- `_Static_assert` is a supported keyword in the spare slot the token
  format reserves for one (`T_KW_SPARE1`, stream code `$9A`). It can't
  be an identifier, as C reserves it.
- cccomp takes it at file scope (`parse_toplevel()`, ptop.c) and as a
  statement in a block (a `case` in `parse_statement()`'s switch,
  pstmt.c, where declarations are already handled, so it may sit
  between declarations), through one function,
  `parse_static_assert()`, in ptop.c.
- The expression goes through a new shared helper, `parse_const()`
  (pexpr.c), that also replaces the three copies of the same check in
  array sizes, enum values and case labels. That should pay for most
  or all of the new code.
- **Strict C on const variables:** a const variable's value isn't a
  constant expression in C (C11 6.6), so it's rejected with message
  201 and a part message, as in array sizes. gcc rejects it too, and
  gcc compiles ccish's own headers, where these assertions will be,
  so an extension couldn't be used there anyway. A comment at the
  check says how to drop the strictness (clang accepts such values).
- A trailing `;` is required, as in both gcc and clang (the C11
  grammar omits it).

## How it works (what we reuse)

1. **Pending constants.** The expression parser (pexpr.c) keeps a
   constant unemitted as `K_CONST` with its value in `o->val`, and
   folds constants that meet, so `2 + 3 == 5` is one `K_CONST`, 1,
   with no code. `sizeof` and enum constants are `K_CONST` too.
2. **Const variables** read as `K_CONST` with `o->nonice = 1`, the
   flag that enforces C's rule (message 201) in array sizes, enum
   values, case labels and initializers.
3. **Code off.** `parse_skipped(min_prec, &o)` parses with `suppress`
   set, so `put()` writes nothing; held operands are dropped after.
4. **Strings take no space.** The message never enters the
   expression parser: it's checked as a `T_STR` token and skipped.
5. **The file and line** come from `error()`, as for every message.

The expression is parsed at `PREC_COND`, as C's constant-expression is
a conditional-expression. (Not `PREC_COMMA`: `binary_prec()` gives `,`
that precedence as an unsupported operator, so parsing at it would
take the comma before the message for a comma operator.)

## Options rejected

- **A reserved identifier with a new fixed id:** `NFIXED` 33 to 34
  and `NAM_VERSION` 2 to 3, renumbering every non-fixed id in every
  name file and token stream, and `_Static_assert` could still name a
  variable. More churn, no saving.
- **An unsupported keyword (`$07` + id):** needs a fixed id too, and
  "not supported yet" would be wrong.
- **A const variable's value as an extension** (as clang does): it
  was the first plan, but ccish's own headers, the reason for the
  feature, are compiled by gcc, which rejects it; with `parse_const()`
  strictness costs no code.
- **An optional `;`:** gcc 15.2 and clang 21.1 both require it.
- **One message per syntax problem:** each `error()` call is code;
  one generic message covers the unlikely ones (missing `(`, `,`, `)`
  or `;`, or a message that isn't a string literal).

## Changes

### ccpp (commit 2, with cccomp, since the token format changes)

1. `src/pplex.c`, `keywords[]`: `"_Static_assert"` after `"union"`,
   so `pp_word()` gives it `TC_KEYWORDS + 26`, with no id. `#if`
   (ppif.c) and the spellings range over `NKEYWORDS`, so they pick it
   up.
2. `src/token.h`: `NKEYWORDS` 26 to 27, `TOK_VERSION` 4 to 5.
3. `CCPP.md`: the keyword table and version.
4. `tests/pp/*.tok`: the `CTK` version byte, 4 to 5.

### cccomp

5. `src/tokin.h`: `T_KW_SPARE1` becomes `T_STATIC_ASSERT`.
6. `src/pexpr.c` and pexpr.h, the shared helper:

   ```c
   // Parse a constant expression (C's: no const variable's value),
   // emitting nothing, and return its value. msg is the error if it
   // isn't an integer constant; part is 201's part message if a const
   // variable's value went into it.
   int parse_const(int msg, int part)
   ```

   It replaces the checks in `parse_dim()`, enum values and case
   labels (pstmt.c). Array sizes move from `parse_expr` to
   `parse_skipped`, which is fine: a size that isn't a constant is an
   error anyway. (Global initializers, ptop.c `parse_scalar_init()`,
   also take addresses, so they keep their own check.)
7. `src/ptop.c`, with its hook in `parse_toplevel()` after the
   `ID_PIMPORT` check:

   ```c
   // Parse a _Static_assert declaration (C11 6.7p6): a constant
   // expression that must be nonzero, a string-literal message, and a
   // ';' (as gcc and clang require). It emits nothing.
   void parse_static_assert(void)
   {
     int v;
     next();                       // _Static_assert
     expect('(', 320 /* "bad _Static_assert, at" */);
     v = parse_const(321 /* "a _Static_assert's expression must be an "
                            "integer constant" */,
                     322 /* "(a _Static_assert's expression must be "
                            "one)" */);
     expect(',', 320 /* "bad _Static_assert, at" */);
     if (tok != T_STR)
       error(320 /* "bad _Static_assert, at" */, tok_desc());
     if (v == 0)
       error(323 /* "static assertion failed:" */, tok_text);
     next();                       // the message
     expect(')', 320 /* "bad _Static_assert, at" */);
     expect(';', 320 /* "bad _Static_assert, at" */);
   }
   ```

   The check of `v` comes before the message is skipped, so
   `tok_text` still holds it for the error, as gcc and clang print it.
8. `src/ptop.h`: its prototype; `src/pstmt.c`: `case
   T_STATIC_ASSERT:` in `parse_statement()`'s switch.

### Messages (ccish.msg; texts cost cccomp nothing)

The keyword spellings are positional (`MSG_KEYWORDS + (tok - T_INT)`),
so `_Static_assert` is 288, and 289 stays free for `T_KW_SPARE2`'s
spelling, so the next keyword doesn't move anything. The operator
spellings, now 288-309, move whole to 324-345 (`MSG_OPS` 288 to 324);
no other number references them. 310-319 stay.

| number | text |
|---|---|
| 288 | `_Static_assert` |
| 289 | (free: the next keyword's spelling) |
| 320 | `bad _Static_assert, at` |
| 321 | `a _Static_assert's expression must be an integer constant` |
| 322 | `(a _Static_assert's expression must be one)` |
| 323 | `static assertion failed:` |
| 324-345 | the 22 operator spellings, `==` to `...`, moved |

### Docs

- ptop.c's header: `_Static_assert` among the declarations.
- README.md: `_Static_assert` among what ccish supports, with its
  narrowing (below).
- approach.md: the decision, with the date.

## Constraints (narrower than C)

1. **Where:** file scope, or a statement in a block; not a `for`
   initializer, a parameter list or a member list.
2. **What:** the expression must fold to an integer constant: integer
   and character constants, enum constants, `sizeof`, casts and the
   operators over them. Pointer constant expressions (`&g == 0`)
   report "must be an integer constant", the same class of difference
   global initializers already have.

## Tests

Expected results are worked out by hand from C, not copied from the
compiler.

Error tests (`tests/errors/NAME.c`, `NAME.stderr.expected`):

1. `static_assert_false.c`: file scope, `_Static_assert(1 == 2, "one
   is not two");`, expecting `static assertion failed: one is not
   two`.
2. `static_assert_nonconst.c`: in a block, on a local variable,
   expecting `must be an integer constant`.
3. `static_assert_constvar.c`: `const int N = 10;
   _Static_assert(N == 10, "x");`, expecting `a const variable's value
   isn't a constant expression in C`.
4. `static_assert_nocomma.c`: `_Static_assert(1);`, expecting `bad
   _Static_assert`.
5. `static_assert_nostring.c`: `_Static_assert(1, 1);`, expecting
   `bad _Static_assert`.

Cross test (`tests/xrun/xstaticassert.c`, its `.ref` from `make xref`,
and an empty `.flags`): assertions at file scope and in a block, and
between a block's declarations, over arithmetic, a macro, an enum
constant, a cast, a conditional (`(1 ? 2 : 3) == 2`) and a character
constant, with no `sizeof` (its value differs on the host).

Gen test (`tests/gen/static_assert.c` and its `.exp`): includes
`sizeof(int) == 2`; its ops must be those of the same file without
the assertions.

## Size

measure.py before and after: cccomp and ccpp code, data and module
size. I expect cccomp to grow well under 100 bytes, or even shrink
(`parse_const()` replaces three copies), and ccpp to grow about 17
bytes (one keyword).

## Commits

1. `parse_const()`, replacing the three checks (no change in
   behavior; `make` passes).
2. The keyword, `parse_static_assert()`, messages, the error and gen
   tests, the `.tok` version bytes, CCPP.md.
3. The cross test and the docs; `make bootstrap`.

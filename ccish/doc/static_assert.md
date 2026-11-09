# Plan: `_Static_assert` (C11 6.7p6)

Implement `_Static_assert ( constant-expression , string-literal )` so
that a false or non-constant assertion stops the build with the file
and line, and a true one generates **nothing downstream**: no IR ops,
no data bytes, no work for ccgen or ccrel. The message string takes no
space in the result at all.

(Plan written 2026-11-08 from an examination of the sources, with
gcc/clang behavior verified on the host the same day. Line references
are as of that day. **Another agent is editing the code concurrently:
re-verify each file before changing it, and agree the commit order
before starting.** No code has been changed for this plan.)

## Summary

- `_Static_assert` becomes a **supported keyword**, taking the first
  spare keyword slot (`T_KW_SPARE1`, stream code `$9A`) that the token
  format already reserves for exactly this (tokin.h: "a new keyword
  takes one of the rest instead of renumbering `T_KEYWORD` and the
  operators").
- cccomp recognizes it at two hook points, file scope
  (`parse_toplevel()`, ptop.c) and the start of a statement in a block
  (`parse_statement()`, pstmt.c), and calls one new function,
  `parse_static_assert()`, in ptop.c.
- The expression is parsed with the **existing** expression parser in
  its "code can never run" mode, `parse_skipped()` (pexpr.c), the same
  mechanism global initializers, enum values and array sizes already
  use to *compute* constants. If the result is a folded nonzero
  constant, the declaration is done: it emitted nothing.
- A **const variable's value counts as a constant** in the assertion:
  a documented extension, as in clang (gcc rejects it; see
  "Declared constants").
- A trailing `;` is **required**, as in both gcc and clang.
- Three new messages: a generic "bad `_Static_assert`" for the syntax
  problems (unlikely; one code, as agreed), "the expression ... is
  not a constant", and "static assertion is false and thus failed"
  (the two cases worth naming separately).
- The framing of ccish's relation to C gains a clause: it is a subset
  of C **with extensions** (README, below).

Total growth in the final small build: roughly **150-250 bytes of
cccomp code, ~17 bytes of ccpp, zero data in either, and zero bytes
in a user program's module** that uses the feature.

## How ccish already computes constants (what we reuse)

Everything needed exists; this feature adds no evaluation machinery.

1. **Pending constants.** The expression parser (pexpr.c) keeps
   constants unemitted as `Operand.kind == K_CONST` with the value in
   `o->val` (parse.h). When two constants meet, `fold_held()` folds
   them (`fold()`) and the result stays pending, so
   `2 + 3 == 5` becomes one `K_CONST` with `val` 1 and **emits no
   code at all**. `sizeof` makes a `K_CONST` too (parse_sizeof()).
2. **Const variables.** `const int N = 10;` is recorded with
   `V_KNOWN`/`var_val[]` (parse.c, set by `set_known()` in pstmt.c),
   and reading `N` gives a `K_CONST` with `val` 10, flagged
   `o->nonice = 1` (the flag that marks "a const variable's value went
   into this"). Array sizes (parse_dim), enum values (parse_enum) and
   initializers (parse_scalar_init, ptop.c) reject that flag with
   message 201 plus a part message (202-205). **For `_Static_assert`
   we simply do not test the flag**: the value is already computed,
   and allowing it is the documented extension.
3. **Code off.** `parse_skipped(min_prec, &o)` (pexpr.c) sets
   `suppress`, under which `put()` (parse.c) is a no-op: every op an
   expression would emit, loads, calls and branches included, is
   checked but discarded, and nothing is left on the IR stream. Labels
   created while skipping are already normal (the skipped side of
   `0 && x` does it).
4. **Strings take no space under suppress.** A string literal's data
   pool entry is made by `data_string()` (irout.c), which
   parse_primary() calls only when `!(suppress && !want_addr)`. Under
   `parse_skipped()` a string in the expression gets `off 0` and no
   pool entry. The message string never enters the expression parser
   at all (it is read as a token and checked, not parsed), so it calls
   `data_string()` never. **Result: the whole declaration, expression
   and message, costs 0 bytes in NAME.ir, NAME.obj and the module.**
5. **Errors carry the file and line for free.** `error()` (diag.c)
   prints `src_name:src_line`, which the token stream's line and file
   tokens maintain (tokin.c). Nothing to add.

## Design options, and the ones rejected

**Option B (chosen): a supported keyword in the spare slot.**
`ccpp`'s lexer (pplex.c `keywords[]`) gets one entry, after `union`;
the word is lexed as token-stream code `$9A` (`TC_KEYWORDS + 26`).
cccomp reads it as `T_STATIC_ASSERT` (the renamed `T_KW_SPARE1`), with
no id in the name file (C keywords get no id, CCPP.md "Names").
Consequences:

- `NAME.nam` files are **unchanged**: no new fixed id, no id
  renumbering, `NAM_VERSION` stays 2. All committed `tests/pp/*.nam`
  stay as they are.
- `tests/pp/*.tok` change by exactly one byte each (the `CTK` version
  byte, `TOK_VERSION` 4 to 5, which CCPP.md requires: "Adding or
  reordering either changes the format version") plus the comment.
- It is a true keyword, as C makes it: `_Static_assert` can no longer
  spell an identifier anywhere (an expression use reports
  "expected an expression, not `_Static_assert`"; C reserves the name,
  so this is conformant and closer to C than the alternatives).
- It costs ccpp ~17 bytes (one pointer and one 15-character string in
  `keywords[]`); ccpp has room (13,817 bytes of code, 26,049 module).
- cccomp needs no id table and works identically in the small build
  (the token code is all it needs).

**Rejected option A: a reserved identifier with a new fixed id**
(no lexer change; `tok == T_ID && tok_val == ID_STATIC_ASSERT` at the
hooks). Rejected because a fixed id must be interned by
`pp_names_init()`, so `NFIXED` 33 to 34 and `NAM_VERSION` 2 to 3, and
**every** name file and token stream renumbers every non-fixed id by
1: all seven `tests/pp/*.tok` and all seven `.nam` files change, and
every future one. It also lets `_Static_assert` be used as a variable
name (C reserves it). Strictly more churn, and a weaker conformance
story, for no size saving in cccomp (the hook is the same size).

**Rejected option B2: an unsupported keyword (`$07` + id) with a
fixed id.** Rejected because it still needs a fixed id (the same
churn as A, and cccomp could not recognize it by number without one,
since an unsupported keyword's id depends on the order names are met),
and the `not_yet()` message ("not supported yet") would be wrong: the
feature is supported, only as a declaration.

**Rejected: strict C on const variables** (rejecting a const
variable's value with the "not a constant" message, as gcc does).
Chosen over this: allowing it, a documented extension as in clang
("Declared constants", below). The user decided 2026-11-08; the
strict variant is also a few bytes *larger* (one extra condition), so
this rejection costs nothing.

**Rejected: making the trailing `;` optional** (the C11 grammar
omits it). Rejected, 2026-11-08: both gcc 15.2 and clang 21.1 require
it (verified: "expected ';' before/after '_Static_assert'" at file
scope, "expected ';' before 'return'" at block scope). The standard's
omission is treated as a spec gap; matching both oracles is simpler
(one `expect(';', 289)`, no `if`) and keeps the cross test free of a
form the oracle would reject.

**Rejected: one message per syntax problem** (missing `(`, missing
`,`, non-string message, missing `)`, missing `;`). Rejected as
agreed: those are unlikely, the user needs the file and line, and
each distinct `error()` call is final code; one generic message
covers them. The two cases worth naming separately ("not a constant",
"false and thus failed") keep their own messages.

## Declared constants (const variables): a documented extension

**Decision (2026-11-08): allow a const variable's value in the
expression, as clang does (gcc does not), and document it as an
extension.**

Verified on the host, 2026-11-08, with
`const int N = 10; _Static_assert(N == 10, "x");`:

- **gcc 15.2.0** rejects it with a hard error,
  "expression in static assertion is not constant", in every mode
  tried: its default (gnu17), `-std=c99` and `-std=c11`, with and
  without `-pedantic`, with and without `-Werror`, and under
  `-fpermissive`. There is no flag that enables it.
- **clang 21.1.8** accepts it, silently in default mode. Under
  `-Wpedantic` it warns, in its `-Wgnu-folding-constant` family,
  "expression is not an integer constant expression; folding it to a
  constant is a GNU extension". Note the warning name: since GNU C
  itself (gcc) does not support this, the leniency is a **clang
  extension**, not a GNU C one.

What C11 says: 6.6p1 and 6.6p4 define constant expressions as built
from integer and character constants, enumeration constants,
`sizeof`/`_Alignof` results, `__LINE__` and the like. A const
variable's value is not among them, so in strict C the expression is
not a constant expression and the declaration violates 6.7p6; a
conforming implementation must diagnose (5.1.1.3), which gcc does.
But the standard does not *forbid* accepting it: 5.1.1.3 also permits
an implementation to accept additional programs (extensions), provided
it accepts all valid programs and behaves as specified on them. So
supporting const values is "not required", not forbidden, and clang
stands as the precedent for accepting them.

In ccish the change is one condition: `N` already folds to
`K_CONST`/`val 10` (item 2 above); the only marker is the `nonice`
flag, which exists to enforce the strict rule at array sizes, enum
values and initializers. `parse_static_assert()` therefore tests
`o.kind != K_CONST || is_pointer(o.type)` and **omits `o.nonice`**.

Scope of the extension: `_Static_assert` only. Array sizes, enum
values and global initializers stay strict (message 201) for now.
approach.md decision 29 already listed relaxing this very rule as a
candidate for array sizes ("the C++ form is the smaller of the two");
doing it there is a separate change, out of scope for this one.

## Semicolon and placement (verified against both oracles)

- A `;` is **required** after the `)`, as in both gcc 15.2 and clang
  21.1 (checked 2026-11-08; the C11 grammar omits it, which we treat
  as a spec gap, and all tests use the `;` form).
- Placement: file scope, or the start of a statement in a block. Not
  in a `for` initializer, a parameter list or a member list (a
  `_Static_assert` there reports the ordinary "expected a type /
  member / parameter" error). C allows it as any declaration; this is
  the documented narrowing.
- A storage class before it is rejected, as C does not allow one on a
  static-assert-declaration: `static _Static_assert` reports
  "expected a type after 'static'".

In a `#if` expression it is a name like any other and becomes 0
(ppif.c ranges over the supported keywords). In an expression context
cccomp reports "expected an expression, not `_Static_assert`"
(message 65, with the spelling looked up as message 288).

## What changes, where

### ccpp (one line in plex.c, three #defines in token.h, one doc,
one byte in each of the seven .tok files)

1. `src/pplex.c`, `keywords[]`: add `"_Static_assert"` after
   `"union"` (the 27th supported entry; the array comment already
   says the list is in `TC_KEYWORDS`/`T_INT` order). `pp_word()` then
   gives it `TC_KEYWORDS + 26` with no id. Everything that ranges over
   `TC_KEYWORDS .. TC_KEYWORDS + NKEYWORDS` (ppif.c `#if` names to 0,
   `pp_spell_token()`'s spellings) picks it up through `NKEYWORDS`.
2. `src/token.h`:
   - `NKEYWORDS` 26 to 27;
   - `#define TC_STATIC_ASSERT (TC_KEYWORDS + 26)` beside
     `TC_STRUCT`/`TC_UNION`;
   - `TOK_VERSION` 4 to 5.
3. `CCPP.md`: the keyword table: the `$98` row gains
   `_Static_assert` at +26; "the order of `T_INT` to `T_UNION`"
   becomes "to `T_STATIC_ASSERT`"; the version note. (While editing,
   note a pre-existing typo in CCPP.md's "Names" section: it calls
   the unsupported-keyword code `$08`, which the token table gives to
   string literals; the unsupported keyword is `$07`. A one-character
   fix may be made in passing.)
4. `tests/pp/*.tok` (7 files): the header line
   `43 54 4b 04   # "CTK", version 4` becomes `... 05   # "CTK",
   version 5`. Nothing else: no name files move, no checksums move
   (verified: NAME.i's stamp is the id count and NAME.nam's checksum,
   written by pp_out_finish(); both are unchanged).

### cccomp (one function, two hooks, message numbers)

5. `src/tokin.h`: rename `T_KW_SPARE1` to `T_STATIC_ASSERT`
   (stream code `$9A`); `T_KW_SPARE2` stays for the next keyword.
   Update the comment. The token reader (tokin.c) needs **no code
   change**: `T_INT + (c - TC_KEYWORDS)` already maps `$9A` to the
   right `T_` slot, and none of the `T_IS_*` ranges reach it.
6. `src/tokin.c`: only the message-number mapping moves (below):
   `#define MSG_OPS 288` to `321`, and the comment "messages 262-287
   are the keywords ... and messages 288-309 the operators" becomes
   "messages 262-288 are the keywords, in T_INT's order, and messages
   321-342 the operators, in T_EQ's order".
7. `src/ptop.c`: `parse_static_assert()` (below) and its hook in
   `parse_toplevel()`, right after the `ID_PIMPORT` check, before
   `T_IS_STORAGE`/`is_type_start()`:

   ```c
   if (tok == T_STATIC_ASSERT) {
     parse_static_assert();
     return;
   }
   ```

8. `src/ptop.h`: `void parse_static_assert(void);` (owner: ptop.c;
   both ptop.c and pstmt.c call it, and pstmt.c already includes
   ptop.h).
9. `src/pstmt.c`: the block-scope hook at the top of
   `parse_statement()`, after the label and typedef checks, before the
   `switch (tok)` (it cannot match either, so the order with them is
   free):

   ```c
   if (tok == T_STATIC_ASSERT) {
     parse_static_assert();
     return;
   }
   ```

10. The new function (ptop.c, with its required comment):

    ```c
    /* Parse a _Static_assert declaration (C11 6.7p6): a constant
     * expression that must be nonzero, a string-literal message, and
     * a semicolon (as in gcc and clang; the C grammar omits it). A
     * const variable's value counts as a constant here (a documented
     * extension; C11 6.6 does not count it, and gcc rejects it). It
     * emits nothing: the expression is checked with the code off
     * (parse_skipped), and the message is only checked, so neither
     * of them takes any data space. */
    void parse_static_assert(void)
    {
      Operand o;
      next();                         /* _Static_assert */
      expect('(', 289 /* "bad _Static_assert" */);
      parse_skipped(PREC_COMMA, &o);
      expect(',', 289);
      if (tok != T_STR)
        error(289 /* "bad _Static_assert" */, tok_desc());
      if (o.kind != K_CONST || is_pointer(o.type))
        error(290 /* "the expression of a _Static_assert is not a "
                   "constant" */);
      if (o.val == 0)
        error(291 /* "static assertion is false and thus failed" */,
              tok_text);
      next();                         /* the string */
      expect(')', 289);
      expect(';', 289);
    }
    ```

    Notes:

    - `PREC_COMMA` parses the whole constant-expression and stops at
      the argument comma (the comma operator itself is not supported
      in ccish anywhere: it reports "not supported yet", as
      elsewhere).
    - The two value errors are tested **before** the string is
      consumed, so on a false assertion `tok_text` still holds the
      message: the error prints it, as in gcc and clang, for the cost
      of passing a global's address instead of NULL (a few bytes). If
      even that is wanted back, drop the second argument to
      `error(291, ...)`.
    - All the other problems (missing `(`, `,`, `)`, `;`; the message
      not being a string literal) share the one generic message 289.
    - `o.nonice` is deliberately not tested (the extension, above).
    - The frame holds one `Operand` (40 bytes, 16-bit), temporary for
      the duration of the call, as in every other expression check.
    - `tests/spec/msgs.sh` (run by `make`) requires every direct
      `error`/`fatal`/`warning` call to carry a `/* "text" */`
      comment exactly matching ccish.msg (the text may be split into
      quoted pieces, as above); `expect()` is not in that check, but
      give its numbers comments too, as every other caller does.

### Messages (ccish.msg; text costs cccomp nothing)

Message text lives only in `ccish.msg`, looked up when printed
(approach.md "C2 design"), so these add no cccomp data. The keyword
spelling for `tok_desc()` must sit at position 288 of the keyword
block (`262 + 26`), since the mapping is positional. The operator
block can move because none of its numbers is referenced literally:
verified by grep, the only literal numbers in 288-319 in `src/` are
the `MSG_OPS` define and its range comment in tokin.c, the `fatal`
calls 310-311 in link.c, 312-314 in parse.c, 315-318 in pstmt.c and
319 in ccrel.c (the 307 in link.c is the `MAX_GSYMS` table size, not
a message). So the operator block moves whole above the numbers
currently in use, and **no existing number moves and no other source
file changes**. The file edits, in order:

- insert 288-291 after line 287 (`union`);
- delete the old lines 288-309 (the 22 operator spellings);
- leave 310-319 exactly as they are;
- append the 22 operator spellings as 321-342, same texts, in order;
- the file must stay in ascending order (msgs.sh enforces it).

| number | text | note |
|---|---|---|
| 288 | `_Static_assert` | keyword spelling, completes 262-288 |
| 289 | `bad _Static_assert` | generic: `(`, `,`, `)`, `;` and the string check |
| 290 | `the expression of a _Static_assert is not a constant` | new |
| 291 | `static assertion is false and thus failed` | new; prints the message after |
| 310-319 | (unchanged) | heap, goto labels, union, list file; not renumbered |
| 321-342 | the 22 operator spellings, `==` to `...` | moved from 288-309, same texts; `MSG_OPS` 288 to 321 |

`tests/spec/msgs.sh` stays green: 289-291 are used in ptop.c's
comments, and 262-288 and 321-342 are covered by the two
`messages ...` range comments in tokin.c. `tests/spec/nomsg.sh` is
unaffected (its `while` is message 274, below the block; its stale
"262-285" comment can be updated to "262-288" while the tokin.c
comment is edited).

### Docs

- `README.md`, "ccish and C" (currently: supports-as-C, left-out,
  known differences): add a fourth clause, in the spirit of approach.md
  decision 29 ("a subset of C89, plus small improvements"):
  "In a few places ccish goes beyond C, accepting things the standard
  does not require (documented extensions): the `pstr()` builtin,
  `#pragma plasma import`, `__asm__("NAME")` labels, the `__CCISH__`
  macro, and, in a `_Static_assert`, the value of a const variable
  (C11 6.6 does not count it a constant expression; clang accepts it,
  gcc does not)." Verify the extension list against the sources when
  editing (decision 29 names `_Bool` as a discussed candidate that did
  not land; function pointers landed 2026-11-07).
- `ptop.c`'s header comment: the supported-constructs list gains
  `_Static_assert` among the declarations.
- `approach.md`: a "decided in discussion" note, with the date, the
  design options, the const-variable decision (with the gcc/clang
  findings) and the required-semicolon decision, with pointers at
  decision 29.

## Documented constraints and extensions

Constraints (narrower than C):

1. **Where:** file scope, or the start of a statement in a block. Not
   in a `for` initializer, a parameter list or a member list. C allows
   it as any declaration; this is the documented narrowing.
2. **What:** the expression must fold to a constant *integer* in the
   expression parser: integer and character constants, enum
   constants, const variables (the extension), `sizeof`, and the
   operators over them. Function calls, reads of non-const variables,
   and pointer values or comparisons (including string literals used
   as values) do not fold and report "not a constant". C11 6.6's
   pointer constant expressions (`&g == 0`, `"a" == "a"`) are the
   documented difference, of the same class as the one global
   initializers already have (ptop.c `parse_scalar_init()`).
3. The comma operator is not supported in ccish anywhere; an attempt
   in the expression reports "not supported yet", as elsewhere.
4. The message must be a string literal (C11 6.7p6); anything else
   reports the generic message.
5. A trailing `;` is required (as in gcc and clang; the C11 grammar
   omits it, which we treat as a spec gap).

Extensions (beyond C):

1. **A const variable's value is a constant** in the assertion (C11
   6.6 does not count it; clang accepts it, gcc does not; see
   "Declared constants").

## Tests

All expected results worked out by hand from C semantics (AGENTS.md),
never copied from the compiler's output. Placement follows decision
29: what the host oracle also accepts goes to `tests/xrun` (the
cross check is the gate); a ccish-only extension goes to
`tests/run`, against hand-written output, and such host-incompatible
extensions are kept few.

**Error tests** (`tests/errors/NAME.c` + `NAME.stderr.expected`, a
`grep -qF` pattern; each file fails the whole compile at one spot):

1. `static_assert_false.c`, file scope, tests the toplevel hook:
   ```c
   _Static_assert(1 == 2, "one is not two");
   int main() { return 0; }
   ```
   `.stderr.expected`: `static assertion is false and thus failed`
2. `static_assert_nonconst.c`, **block scope**, tests the statement
   hook:
   ```c
   int main()
   {
     int x = 1;
     _Static_assert(x, "a variable is not a constant");
     return 0;
   }
   ```
   `.stderr.expected`: `the expression of a _Static_assert is not a constant`
3. `static_assert_constvar.c`, the extension through a false
   assertion (proves the const value folds and the failure path sees
   it):
   ```c
   const int N = 10;
   _Static_assert(N == 11, "ten is not eleven");
   int main() { return 0; }
   ```
   `.stderr.expected`: `static assertion is false and thus failed`
4. `static_assert_noparen.c`, with source `_Static_assert 1;`, pattern
   `bad _Static_assert`
5. `static_assert_nocomma.c`, with source `_Static_assert(1);`, pattern
   `bad _Static_assert`
6. `static_assert_nostring.c`, with source `_Static_assert(1, 1);`,
   pattern `bad _Static_assert`

**Run test** (the ccish-only extension, so not a cross test):
`tests/run/static_assert.c` + `static_assert.expected`:

```c
/* The _Static_assert extension: a const variable's value counts as a
 * constant (decision 29's candidate; clang accepts it, gcc does not,
 * so this is a run test, not a cross test). */
const int WIDTH = 40;
_Static_assert(WIDTH + 24 == 64, "a const variable");
_Static_assert(WIDTH * 2 == 80, "arithmetic on it");
int main()
{
  _Static_assert(1, "in a block");
  __puts(pstr("ok"));
  return 0;
}
```
`.expected`: `ok` (as in `hello.expected`: no trailing newline).

**Cross test** (`tests/xrun/xstaticassert.c` + `.ref` from
`make xref` + an empty `.flags` file, as every xrun test has one):
oracle-safe set, `;` after every assert, no const variables (gcc
rejects them). Every expression is a
constant in both a 16-bit and a wider int (ptest.h contract; no
`sizeof`, the one size-dependent construct):

```c
#include "ptest.h"

#define TEN    10
enum { E_ONE = 1 };

_Static_assert(1 + 1 == 2, "arithmetic");
_Static_assert(TEN * 2 == 20, "a macro");
_Static_assert(E_ONE == 1, "an enum constant");
_Static_assert((int)0 == 0, "a cast");
_Static_assert(1 ? 2 : 3 == 3, "a constant conditional");
_Static_assert((char)65 == 'A', "a char constant");

int f(int x)
{
  _Static_assert(1, "in a block");
  return x + 1;
}

int main()
{
  putc('o'); putc('k'); putln();
  return f(41) - 42;
}
```
(`make xref` runs the oracle and fails the build if the source is not
clean C under `-Wall -Wextra -Werror`, so this file doubles as the
conformance check for the forms used.)

**Gen test** (`tests/gen/static_assert.c` + `.exp`): proves nothing is
emitted, for the plain cases: the `.exp` (machine ops after the
peephole) is identical to the same file with the asserts removed, and
the module runs to exit status 0 (default; no `.status` file):

```c
_Static_assert(2 + 3 == 5, "five");
_Static_assert(sizeof(int) == 2, "an int is 16 bits");
int f(int x)
{
  return x + 1;
}
int main()
{
  return f(41) - 42;
}
```
(`sizeof(int) == 2` is ccish-specific, so it lives here, not in the
cross test. The `.exp` is computed by hand; "same as without the
asserts" is a sanity check, not the source of it.)

## Size, measured after

Per AGENTS.md ("always consider space"), after the change:

- `python3 tools/measure.py` before (a worktree at the previous
  commit) and after: report the cccomp and ccpp deltas in code, data,
  strings and module file. Expected: cccomp +150-250 bytes of code
  (the function and the two hooks; the `T_` rename is free), +0 data
  (messages are in the file); ccpp +~17 bytes; module files grow by
  the code only. If cccomp grows more than ~250 bytes, say so and
  look for the cause (the frame, or a call the peephole did not fold).
- `make` must pass in full, including the small-build byte-identical
  objects check and `tests/spec/msgs.sh` and `comments.sh`.
  `tools/opcount.py` is not needed (no generated code changes for
  existing tests).

## Verification and commit order

1. **Commit 1, ccpp:** plex.c, token.h, CCPP.md, the seven `.tok`
   version bytes. `make` passes (pp tests re-run against the new
   ccpp; nothing in the suite uses the new keyword yet).
2. **Commit 2, cccomp + the non-cross tests:** tokin.h, tokin.c,
   ccish.msg, ptop.c, ptop.h, pstmt.c, the six error tests, the run
   test and the gen test. `make` passes.
3. **Commit 3, cross test + docs:** `make xref` to write
   `tests/xrun/xstaticassert.ref`, then the README.md and approach.md
   notes. `make` (or `make full-test`, which also runs the bootstrap,
   to be sure the self-compile is unaffected) passes.
4. Each commit ends with the usual `Assisted-by:` / `Signed-off-by:`
   lines (AGENTS.md). Never push.

## Open questions (none blocking)

- The exact extension list for the README clause (verify against the
  sources; function pointers landed 2026-11-07, `_Bool` was only
  discussed).
- Whether to keep the message text in the "false" error (default:
  keep it; it costs a few bytes and is the point of the feature; both
  oracles print it too).

# What Rod's Colors shows about ccish's optimizers

`samples/rod.c` and `samples/rodsimple.c` are the same demo, Rod's
Colors, written two ways: rodsimple plainly (the original's loops, plot
by plot), and rod hand-optimized for speed. Both draw exactly the
original's screens (`make -C samples check` compares a checksum after
every frame with `samples/rodref.c`, the original compiled with gcc).
The gap between them, and the code ccish makes for each, show where
the optimizers could do more. This lists those places, with my
estimate of what each is worth, roughly in order of payoff.

All numbers are from an op-counting plvm (as `tools/opcount.py` builds
it), with each op weighted by its 6502 cycles from
[../../doc/Bytecodes.md](../../doc/Bytecodes.md) (taken branches at
their larger count). A frame is one pass over i, for one w. Measured
2026-11-09:

| Version | VM ops a frame | Seconds a frame at 1.023 MHz |
|---|---|---|
| rodsimple.c | 65,300 | 4.33 |
| rod.c | 10,200 | 0.57 (and 1.25 once, at the start, for its tables) |
| rod.pla (PLASMA) | | about 2.3, estimated: its `conio:grplot` is native code, which plvm doesn't run |

Most of rod's gain is algorithmic: it draws only the 1,520 pixels a
frame that show at the frame's end, not 3,040 plots, and only those
whose colors changed (986 a frame); no compiler would find that. The
rest is code a compiler could make from plain C, and that's what's
listed here.

## Compiler: front end and back end

1. **Loop-invariant code motion.** rodsimple computes
   `i * w / W_DIV` (a MUL and a DIV, about 1,430 cycles) and
   `SIZE - i` in its inner loop over j, which changes neither; hoisted
   out of that loop, they'd cost once per i. That's about 13% of
   rodsimple's time. The same gap made rod's table builder recompute
   `SIZE - J_MAX - i`, a loop limit, on every pass, which also kept the
   loop from being counted, and index `qlo[i][t]` with a MUL by 41
   on every access (`qlo[i]` is invariant). approach.md's "Hoisting
   moved globals' addresses" considered a narrower form and deferred
   it. This belongs in ccgen, which has room: its loop buffer already
   holds a loop's test and step, and the test needs the same "nothing
   in the body changes it" rule a counted loop's limit does. I think
   pure expressions of locals not stored in the body, and of
   constants, are the case worth doing first.

2. **Common subexpressions in a read-modify-write.** `LORES_PLOT`'s
   `*(p) = (*(p) & m) | n`, with `p` = `lores_row[y] + x`, computes the
   address twice: `LA lores_row IDXLW y LW ADDLW x`, 4 ops and about
   256 cycles each time. Kept in a temporary instead (DLW once, LLW
   after), the second costs about 116 cycles less. That's about 10% of
   rodsimple's time. For a 2-op address (`LLW p ADDLW x`, as in rod's
   loops) it doesn't pay. Also in rodsimple: `c << LORES_HI_SHIFT` is
   computed for each of the up to 8 odd-row plots of an (i, j), about
   4%. ccgen would need to recognize the same pure expression twice
   within a statement (or a basic block) and to own a frame slot for
   the temporary.

3. **Counted loops nested in other loops, with stores through
   pointers.** Every inner loop in both programs stores through a
   pointer or into an array, so none nested in another loop is
   counted (approach.md, "Possible improvements to the loop forms",
   item 4). rod had to move each such loop into a function (`draw`,
   `make_ring`) to make it outermost: about 4 ops less a pass, which
   was 5% of rod's time when I did it (12,100 ops a frame to 11,500). Item 4 proposes allowing
   stores into arrays; rod's loops store through pointer variables,
   which that wouldn't cover. A rule that would: a store through any
   pointer can't change a local whose address is never taken in the
   function (C11 6.5.6, 6.5.3.2: a pointer can reach an object only
   through its address). That needs cccomp to mark each local whose
   address is taken (`&x`, or an array decaying), which it could put
   in the IR at the function's end.

   *Status (2026-11-09):* not done. Neither cccomp nor ccgen sees the
   rest of the function when it decides (both stream), so "never taken"
   needs a look ahead over each function, more than it's worth. A
   cheaper route is C's `register`: `&` on a register variable is a
   constraint violation (C11 6.5.3.2p1), and a register array can't
   decay to a pointer, so no pointer can reach one. A nested loop whose
   variable and local limit are `register` could be counted even with
   stores through pointers. ccish rejects `register` today (guide 9.2).
   Proposed, not built: cccomp would accept it, reject `&` on it (also
   in the small build, since C requires the diagnostic), and mark the
   local in the IR; ccgen would relax the nested-loop rule for marked
   locals. My estimate is 60 to 100 bytes in each of cccomp and ccgen.

4. **A `?:` whose value isn't used.** In an expression statement,
   `c ? (a = x) : (a = y);` computes the chosen assignment's value and
   drops it (`DUP` before the store, `ANDI 255` after, `DROP` at the
   join): 3 ops for nothing. With a `(void)` cast it's worse: each arm
   reloads the stored byte (`LB`) as the value. `LORES_PLOT` was such a
   `?:`; as `do { if ... else ... } while (0)` it costs nothing extra,
   and rodsimple got 7% faster. The fix is to pass "value unused" into
   both arms, as for a plain assignment statement.

   *Status (2026-11-09):* built and tested, but not applied: it costs
   cccomp 71 bytes of code (113 in its module file), cccomp doesn't fit
   yet, and only a `?:` used as a whole statement gains (no change in
   ccish's own programs). The patch is `../,void-cond.patch`, kept for
   a decision: parse_conditional() drops each side's value when the
   `?:` is the statement's own operator (its nesting depth, set by
   parse_expr_statement()), so an assignment side just stores.

5. **A store, then a load of the same local, apart.** rod's row loop
   computes the color word `s2`, stores it (`SLW`), loads the screen
   word and masks it, then loads `s2` back to OR it in. If the masked
   screen word came first, `IOR`'s other operand, and `s2` were
   computed second, the store and its first load would be one `DLW`.
   IOR, AND, XOR, ADD and MUL commute, so ccgen could put the operand
   that ends in a store to a temporary last. About 3% of rod's row
   loop.

6. **A load of a local twice in a row.** `LLB 15 LLB 15` (from
   `b16 | (b16 << 8)`) could be `LLB 15 DUP`: DUP takes 44 cycles,
   LLB 54 and LLW 59, so DUP saves 10 to 15 cycles, and LAW (72) or
   LAB (61) more. peep.c makes a DUP only for two equal `CW`
   constants, though the guide (10.3) says "a duplicate load is a
   DUP": either the guide or peep.c should change. Safe for a local
   that isn't volatile. Small for rod (once a ring), but cheap to add.

   *Status (2026-11-09):* done (peep.c, `repeats_load()`): two equal
   loads of a local or global (`LLB`, `LLW`, `LAB`, `LAW`), or a keeping
   store and a load of what it kept (`DLW n LLW n`), make the second a
   `DUP`, once a third op follows that no other rule joined to the
   load, so `x + x` stays `LLW ADDLW`. A volatile op is odd, and
   matches none of them. ccgen +142 bytes; ccpp's code -6, cccomp's -7.
   New test: `tests/gen/gendup.c`. Where the repeated load was a
   local's last read, the store before it is then dead at a `LEAVE`,
   and goes: `y = x + 1; return y * y;` is `LLW INCR DUP MUL`.

7. **Multiplying a byte by 257.** `b * 0x0101` (a byte in both bytes
   of a word) is a MUL, about 580 cycles; for a `char` b,
   `b | (b << 8)` is about 230. rod writes the shift itself. Possible
   as a peephole on a byte load followed by `CW 257 MUL`, but rare.

8. **Address constants from cast integers.** In a global initializer
   ccish folds `buf + 128` and `(char *)0x480`, but not
   `(char *)0x400 + 128` ("initializer is not a constant ccish
   supports"); lores.c adds first, then casts. A cast constant plus a
   constant is still an address constant (C11 6.6p9), so cccomp should
   fold it like the others.

   *Status (2026-11-09):* done (pexpr.c, `fold_held()`): a constant
   pointer before `+` or `-` is held back like an integer constant, and
   folds with a constant right side, scaled by the element size, in a
   global's initializer as in code. cccomp +28 bytes of code (+35 for
   the fold, -7 from the DUP rule). New tests: `tests/run/ptr_const.c`,
   and `tests/errors/ptr_const_void.c` (`void *` arithmetic is still an
   error). lores.c now writes `(char *)PAGE1 + OFF(r)`.

9. **ccpp's 255-character limits.** A macro's continuation lines,
   joined, must fit ccpp's 255-character line, padding included, and
   its expansion has a limit too ("macro expansion too long"). Macros
   are how ccish code inlines a hot loop's body (guide 12.3), so rod
   splits its drawing macros into six small ones. Raising the limits
   costs ccpp memory; dropping the padding before a `\` when joining
   would ease the first at no cost.

   *Status (2026-11-09):* not done. A backslash-newline may splice a
   string literal, where the spaces are part of the string, so
   collapsing them safely means knowing whether the join is inside a
   literal.

## The VM

Changing the VM means 7 VMs and 3 JITs (approach.md, issue 21), so
these are for a later VM, in order of payoff for code like rod's:

1. **Load and store through a local pointer plus a local index.**
   rod's row loop does `LLW p ADDLW x` then `LW` or `SW` six times a
   pass, each 3 ops; as one op, each would save 2 dispatches, about
   20% of the loop. PLASMA's own `ptr[i]` would use it too.
2. **A store with the address below the value, or SWAP.** Every
   read-modify-write computes its address twice, since a store takes
   the value below the address and there's no way to move a `DUP`ed
   address below a value. With either, `*p = (*p & m) | n` needs the
   address once.
3. **16-bit AND and OR with an immediate word.** `CW 0xF0F0 AND` is 2
   ops, about 94 cycles; `ANDI` and `ORI` take only a byte. rod's row
   loop has 3 a pass.

## Native code

None of the above gets near native code: a small 6502 routine to
draw one row of a ring would take about 25 cycles a pixel, where
rod's bytecode loops take about 350 to 450. ccish can call 6502 code only
through a function pointer to it (the guide, `__call` in section 11),
so a program would carry the routine as bytes. If it's worth having
for programs like this, I think the clean way is something like
PLASMA's `asm` functions (approach.md, "Native code"), not hand-made
byte arrays.

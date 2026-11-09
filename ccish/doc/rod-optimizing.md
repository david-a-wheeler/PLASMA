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
| rodsimple.c | 61,900 | 4.09 |
| rod.c | 10,200 | 0.57 (and 1.25 once, at the start, for its tables) |
| rod.pla (PLASMA) | | about 2.6, estimated from `xplasm -AMOS`'s inner loop (73 ops, about 5,830 cycles) and the native code it calls (about 1,140): plvm doesn't run `conio:grplot`'s 6502 code |
| rodconio.c | | about 2.1 (2.09), estimated the same way: ccish's inner loop (70 ops, about 4,480 cycles) and the same native code; 2.61 without `register` |

rodconio.c is rod.pla's program in C: the same loops, calling the same
CONIO routines, with `char` variables where rod.pla has `byte` ones.
Without `register`, ccish's code is within 1% of PLASMA's compiler's,
op for op the same but for the loop over j, which isn't counted, since
its body makes calls (approach.md, loop improvement 3): 6 ops of loop
work a pass where rod.pla's takes 2. With its variables `register`, as
it now has them, ccish computes `i * w / W_DIV` once for each i
instead of on every pass over j (a MUL and a DIV, about 1,400 cycles),
which PLASMA's compiler doesn't, and runs about 19% faster than
rod.pla.
rodsimple.c is slower than both only because it plots in bytecode,
about 1,000 cycles a pixel, where CONIO's `grplot` is 6502 code, about
360 with its call.

Most of rod's gain is algorithmic: it draws only the 1,520 pixels a
frame that show at the frame's end, not 3,040 plots, and only those
whose colors changed (986 a frame); no compiler would find that. The
rest is code a compiler could make from plain C, and that's what's
listed here.

## Compiler: front end and back end

1. **Loop-invariant code motion.** *(Built 2026-11-09, narrowly:
   approach.md, "`register` is built": costly invariant expressions in
   the innermost loop, whose leaves are register locals, or locals of
   an outermost loop whose addresses are never taken.)* rodsimple computes
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
   `*(p) = (*(p) & m) | n`, with `p` = `lores_row[y] + x`, computed the
   address twice: `LA lores_row IDXLW y LW ADDLW x`, 4 ops and about
   256 cycles each time. The VM can't keep the first copy for the
   store, which takes the value below the address, and there's no
   `SWAP`. Kept in a temporary instead, the address costs a store and
   two loads (`SLW`, then `LLW` twice; the parity test between them
   keeps the store from being a `DLW`), about 77 cycles a plot less.
   I first estimated 10% of rodsimple's time, assuming a `DLW` and
   one `LLW`; measured, it's 5% (4.33 s a frame to 4.11).
   `LORES_PLOT` now does this itself, in a block-local pointer. For a
   2-op address (`LLW p ADDLW x`, as in rod's loops) it doesn't pay.
   Also in rodsimple: `c << LORES_HI_SHIFT` is computed for each of the
   up to 8 odd-row plots of an (i, j), about 4%. In the compiler, this
   belongs in ccgen, which would need to recognize the same pure
   address twice (13 ops or more apart, past the peephole's 10-op
   window, so on the IR) and own a frame slot for the temporary: a few
   hundred bytes, I guess, for 5 to 10% in code like this. A VM store
   with its operands the other way round, or `SWAP`, would do better
   with no temporary (about 250 cycles a plot; "The VM", item 2).

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
   Built 2026-11-09 (approach.md, "`register` is built"): cccomp +59
   bytes, and in ccgen, counted nested loops over register locals,
   `!=` tests at any depth, and hoisting.

4. **A `?:` whose value isn't used.** In an expression statement,
   `c ? (a = x) : (a = y);` computes the chosen assignment's value and
   drops it (`DUP` before the store, `ANDI 255` after, `DROP` at the
   join): 3 ops for nothing. With a `(void)` cast it's worse: each arm
   reloads the stored byte (`LB`) as the value. `LORES_PLOT` was such a
   `?:`; as `do { if ... else ... } while (0)` it costs nothing extra,
   and rodsimple got 7% faster. The fix is to pass "value unused" into
   both arms, as for a plain assignment statement.

   *Status (2026-11-09):* not done. It was built (cccomp +71 bytes),
   then dropped in discussion: `c ? (a = x) : (a = y);` is unusual, and
   the usual `a = c ? x : y;` already compiles to one load per side and
   one store after the join, with nothing to drop.

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

   *Status (2026-11-09):* done for the line limit: ccpp now joins a
   line that ends in a backslash as its lexer reads (pplex.c,
   `splice()`), so the 255 characters are each physical line's, and a
   macro may take as many lines as it needs. ccpp +70 bytes. The limit
   on a macro's expansion is as it was.

// ccgen's loop layout (approach.md, "Loop design"): the loop markers
// the parser writes, the held headers and the DLW fix (step 1), and
// counted loops (step 2). irin.c passes each IR op here first
// (loop_take()); what a loop doesn't take, irin.c holds itself. This is
// apart from irin.c so that each file's variables fit cccomp's
// MAX_VARS.
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "ir.h"
#include "mop.h"
#include "irloop.h"

// Loops (approach.md, "Loop design"). The parser writes a loop in
// source order, between markers that name its labels:
//   LOOP Ltest Lbrk  test  LOOPSTEP Lcont Lbrk  step
//   LOOPBODY Lbody Ltest  body  LOOPEND Lcont Lbrk
// where the test branches to Lbody when true. Here the test and step
// are held (lp_op[] and the rest) instead of lowered, and lowered after
// the body, so that each pass runs one branch:
//   BRNCH Ltest  Lbody: body  Lcont: step  Ltest: test  Lbrk:
// The entry BRNCH is left out when the test is just a jump to Lbody
// (for (;;)), and a held loop's labels are placed only if something
// branches to them (lab_ref[]), since a label stops the peephole
// stage's patterns, and code after a jump that no label follows can
// be dropped (while (0)). Loops nest: held headers form a stack in lp_op[], one
// loop's test from lp_test[d], its step from lp_step[d], up to the
// next loop's or nlp; only the innermost open header is ever filling.
// A header that doesn't fit, or a loop nested more than LOOP_NEST deep,
// is lowered in place, as the jumps around the step would have been
// (no held state is needed then: the markers name every label):
//   Ltest: test  BRNCH Lbrk  Lcont: step  BRNCH Ltest
//   Lbody: body  BRNCH Lcont  Lbrk:
#define LOOP_BUF  128       // held ops, for every open loop's header,
                            // and a counted loop's body
#define LOOP_NEST 8         // loops held at once
static char lp_op[LOOP_BUF];
static int lp_a[LOOP_BUF];
static int lp_f[LOOP_BUF];
static int nlp;
static int lp_test[LOOP_NEST];  // where each open loop's test starts
static int lp_step[LOOP_NEST];  // and its step, or -1 until LOOPSTEP
static int lp_ltest[LOOP_NEST]; // its test's label
static int lp_lcont[LOOP_NEST]; // and its continue and break labels,
static int lp_lbrk[LOOP_NEST];  // from LOOPSTEP
static int lp_lbody[LOOP_NEST]; // and its body's, from LOOPBODY
static char lp_held[LOOP_NEST]; // its header is held
static int lp_fvar[LOOP_NEST];  // the DLW fix: its variable, or -1
static char lp_fload[LOOP_NEST];    // and how the test loads it
static char lp_fpost[LOOP_NEST];    // the step is v++ or v--
static int lp_depth;            // loops open
static int lp_fill;             // the innermost loop's header (1), or
                                // its body (2: it may be counted), is
                                // being held
static char lab_ref[MAX_LABELS];    // a branch to it has been read

// Counted loops (approach.md, "Loop design", step 2): a loop whose test
// and step have the shape of a counted loop (counted_shape()) has its
// body held too (lp_fill 2, from cl_body), so that at LOOPEND ccgen has
// seen all of it, and can keep the count and the limit on the eval
// stack, as PLASMA's FOR does: 2 or 3 ops of the loop's own work per
// pass, instead of 7. The body must not change the loop variable
// (cl_var, a frame offset) or anything the limit reads (limit_ok()),
// take the addresses of the variable or the limit's locals, call anything
// (the two values stay on the eval stack across a call, and a body
// that recursed would fill it), or hold a loop; nor, in a loop
// inside another loop, or after a goto label (goto_seen), store
// through a pointer (a pointer to the variable might be taken later in
// the outer loop, before this one runs again). Their addresses mustn't have been taken earlier in the
// function either (addr_taken[]). Anything else in the body, or a body
// too big for lp_op[], makes it a loop like any other (body_plain()).
// A return in the body first drops the two values under its result,
// which waits meanwhile in temporaries (replay_body()).
//
// A loop that isn't counted, whose test starts by pushing a local v
// (LOCAL v, or LOCALB v for a char) and holds no SWAP (whose blocks
// could reach back past Ltest), and whose step ends by storing v
// (SETLOCAL v or SETLOCALB v, then DROP), gets the DLW fix (approach.md,
// "The DLW fix", and "for any store"): the entry pushes v before it
// jumps to Ltest, and the step, without its DROP, keeps the v it
// stores (DLW or DLB), so the test's own load is left out:
//   LLW v  BRNCH Ltest  Lbody: body  Lcont: step, ending DLW v
//   Ltest: the rest of the test
// one op less per pass, in the same bytes. v++ and v-- (LOCAL v, DUP,
// CONST 1, ADD or SUB, SETLOCAL v, DROP, DROP: the old value is
// dropped last) are lowered without the DUP and both DROPs, which is
// ++v (lp_fpost[]).
static int cl_body;             // where the held body starts
static int cl_var, cl_cmp, cl_step, cl_nested;
// The limit: held ops cl_ls up to cl_le, computed once on entry; a
// constant if cl_const (then cl_lim). The locals it reads (cl_loc[]),
// the globals (cl_gf[] the symbol or -1, cl_ga[] the offset), and
// whether it loads through an address (cl_load), which the body then
// mustn't change.
#define LIM_NAMES 4
static int cl_ls, cl_le, cl_const, cl_lim, cl_load;
static int cl_loc[LIM_NAMES], cl_nloc;
static int cl_gf[LIM_NAMES], cl_ga[LIM_NAMES], cl_ngl;
static unsigned char addr_taken[32];    // frame offsets: a bit each
// A goto label has been placed in this function: a goto back to it
// makes a loop around what follows, as an enclosing loop does, so a
// loop after it counts as nested (code after the loop may take its
// variable's address before the goto runs it again).
static char goto_seen;

// A loop that can't be counted (a call in its body, say) can still end
// at != when it has a counted loop's shape, steps by 1, and has a
// constant limit: v <= 19 is v != 20 (BRNE, one op) once v starts at a
// constant no further than 20 and nothing else changes v. Only for an
// outermost loop with no goto label before it or in it (goto_seen),
// whose variable's address isn't taken (counted_shape()): then nothing
// but the step can change v, since a pointer can't reach it, and code
// after the loop can't run before it. ne_ok says the outermost open
// loop is such a candidate, so far: ne_var its variable, ne_stop where
// it stops, and ne_ci and ne_cmp the held test's constant and
// comparison, to make CONST ne_stop and NE at LOOPEND.
static int ne_var, ne_stop, ne_ci, ne_cmp;
static char ne_ok;

// Hold a jump to label n.
static void jump(int n)
{
  lab_ref[n] = 1;
  ir_hold(IR_JUMP, n, -1);
}

// Hold label n, if something branches to it.
static void label(int n)
{
  if (lab_ref[n])
    ir_hold(IR_LABEL, n, -1);
}

// Hold machine branch op to label n (a counted loop's).
static void mop_branch(int op, int n)
{
  lab_ref[n] = 1;
  ir_hold(IR_MOP, op, n);
}

// Lower held ops from from up to (not including) to.
static void replay(int from, int to)
{
  for (; from < to; from++)
    ir_hold(lp_op[from], lp_a[from], lp_f[from]);
}

// The innermost loop's header doesn't fit: lower what it holds, and
// the rest of the loop in place.
static void loop_overflow(void)
{
  int d = lp_depth - 1, t = lp_test[d], s = lp_step[d];
  ir_hold(IR_LABEL, lp_ltest[d], -1);
  replay(t, s < 0 ? nlp : s);
  if (s >= 0) {
    jump(lp_lbrk[d]);
    ir_hold(IR_LABEL, lp_lcont[d], -1);
    replay(s, nlp);
  }
  nlp = t;
  lp_held[d] = 0;
  lp_fill = 0;
}

// Hold the jump into open loop d's test, after pushing the variable for
// the DLW fix.
static void enter_test(int d)
{
  if (lp_fvar[d] >= 0)
    ir_hold(lp_fload[d], lp_fvar[d], -1);
  jump(lp_ltest[d]);
}

// The innermost loop's held body can't be counted: lower what it holds,
// as step 1 does (its test is never just a jump), and the rest as it
// comes.
static void body_plain(void)
{
  int d = lp_depth - 1;
  enter_test(d);
  label(lp_lbody[d]);
  replay(cl_body, nlp);
  nlp = cl_body;
  lp_fill = 0;
}

// Hold IR op op (with a and f) of the header or body being filled.
static void keep(int op, int a, int f)
{
  if (nlp == LOOP_BUF) {
    if (lp_fill == 2)
      body_plain();
    else
      loop_overflow();
    ir_hold(op, a, f);
    return;
  }
  lp_op[nlp] = (char)op;
  lp_a[nlp] = a;
  lp_f[nlp] = f;
  nlp++;
}

// Has frame offset off's address been taken in this function?
static int taken(int off)
{
  return (addr_taken[off >> 3] >> (off & 7)) & 1;
}

// Does offset a reach the word at offset b (the same, or a byte of it)?
static int near(int a, int b)
{
  return a - b <= 1 && b - a <= 1;
}

// Is frame offset off the loop variable's, or a local limit's (a word
// each, so a byte of either)?
static int counted_var(int off)
{
  int i;
  if (near(off, cl_var))
    return 1;
  for (i = 0; i < cl_nloc; i++)
    if (near(off, cl_loc[i]))
      return 1;
  return 0;
}

// Might a store to global offset a (of symbol f, or -1) change what the
// limit reads? Any global store might, if the limit loads through an
// address (an array's element); else one that overlaps a global it
// reads (a word or a byte, so within a byte either way).
static int limit_global(int a, int f)
{
  int i;
  if (cl_load)
    return 1;
  for (i = 0; i < cl_ngl; i++)
    if (cl_gf[i] == f && near(a, cl_ga[i]))
      return 1;
  return 0;
}

// Are held ops from up to (not including) to a limit a counted loop can
// compute once: one value, of constants, locals other than the variable
// (whose addresses haven't been taken), globals, addresses, loads
// through them, and arithmetic, with no side effects? Sets cl_loc[]
// and the rest. A volatile load (such as the Apple's I/O, where a read
// may differ each time) isn't one: it has its own op, so none here.
static int limit_ok(int from, int to)
{
  int depth = 0, op, a;
  cl_nloc = cl_ngl = cl_load = 0;
  for (; from < to; from++) {
    op = lp_op[from];
    a = lp_a[from];
    switch (op) {
    case IR_CONST:
      depth++;
      break;
    case IR_LOCAL: case IR_LOCALB:
      if (near(a, cl_var) || taken(a))
        return 0;               // the variable, or reachable
      if (!counted_var(a)) {
        if (cl_nloc == LIM_NAMES)
          return 0;
        cl_loc[cl_nloc++] = a;
      }
      depth++;
      break;
    case IR_GLOBAL: case IR_GLOBALB:
      if (cl_ngl == LIM_NAMES)
        return 0;
      cl_gf[cl_ngl] = lp_f[from];
      cl_ga[cl_ngl++] = a;
      depth++;
      break;
    case IR_DATAADDR: case IR_HEAPADDR: case IR_STRADDR: case IR_FUNCADDR:
      depth++;
      break;
    case IR_LOAD: case IR_LOADB: case IR_NEG: case IR_COMP:
      if (depth < 1)
        return 0;
      cl_load |= IR_WORD_FORM(op) == IR_LOAD;
      break;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
      if (depth < 2)
        return 0;
      depth--;
      break;
    default:
      return 0;
    }
  }
  return depth == 1;
}

// May IR op op (with a and f) be in a counted loop's body? Not a goto
// or a goto label (decision 34), on purpose. A counted body runs with
// its limit and count on the stack under its code, so every entry into
// the body must leave them there. A label in the body is an entry, and
// a forward goto can reach it from before the loop, where the limit
// and count are not on the stack and we do not know how deep the stack
// is, so we cannot place them; and a goto that jumps out of the body
// skips the loop's drops, leaving the limit and count behind. Such a
// loop is kept uncounted and takes the plain form. Gotos always work
// in a function; this only switches off the counted optimization. A
// goto is an IR_JUMP to a goto label (ir.h), so both are told by the
// label's number.
static int body_ok(int op, int a, int f)
{
  // A volatile access is checked as its plain form: what it stores to
  // matters here, not how often it runs (the body runs it as written).
  switch (IR_IS_ACCESS(op) ? IR_PLAIN_FORM(op) : op) {
  case IR_CALL: case IR_ICALL: case IR_UDIV: case IR_UMOD: case IR_USHR:
    return 0;
  case IR_LABEL: case IR_JUMP:
    return !IS_GOTO_LABEL(a);   // a goto, or its target
  case IR_SETLOCAL: case IR_SETLOCALB: case IR_LOCALADDR:
    return !counted_var(a);
  case IR_STORE: case IR_STOREB:
    return !cl_nested && !cl_load && !cl_ngl;  // a pointer might reach
  case IR_SETGLOBAL: case IR_SETGLOBALB:
    return !limit_global(a, f);
  }
  return 1;
}

// Is open loop d's held step (up to nlp) v++ (LOCAL v, DUP, CONST k,
// ADD or SUB, SETLOCAL v, DROP, DROP), or ++v or v += k (the same
// without the DUP and a DROP), for a local v? If so, set *v, and *k
// (negative for SUB).
static int step_shape(int d, int *v, int *k)
{
  int st = lp_step[d], n = nlp - st, u;
  if (n != 5 && n != 7)
    return 0;
  u = n == 7;                   // the DUP of v++
  if (lp_op[st] != IR_LOCAL || (u && lp_op[st + 1] != IR_DUP)
      || lp_op[st + 1 + u] != IR_CONST
      || (lp_op[st + 2 + u] != IR_ADD && lp_op[st + 2 + u] != IR_SUB)
      || lp_op[st + 3 + u] != IR_SETLOCAL
      || lp_a[st + 3 + u] != lp_a[st] || lp_op[nlp - 1] != IR_DROP
      || lp_op[st + 4 + u] != IR_DROP)
    return 0;
  *v = lp_a[st];
  *k = lp_op[st + 2 + u] == IR_ADD ? lp_a[st + 1 + u] : -lp_a[st + 1 + u];
  return 1;
}

// Does open loop d's held header suit the DLW fix? If so, set
// lp_fvar[d] and the rest; else lp_fvar[d] is -1.
static void dlw_fix(int d)
{
  int t = lp_test[d], st = lp_step[d], v = lp_a[t], load = lp_op[t];
  int store = IR_STORE_FORM(load), i;
  lp_fvar[d] = -1;
  if (st - t < 2 || (load != IR_LOCAL && load != IR_LOCALB))
    return;
  for (i = t; i < st; i++)
    if (lp_op[i] == IR_SWAP)
      return;
  if (nlp - st == 7 && lp_op[st] == load && lp_a[st] == v
      && lp_op[st + 1] == IR_DUP && lp_op[st + 2] == IR_CONST
      && (lp_op[st + 3] == IR_ADD || lp_op[st + 3] == IR_SUB)
      && lp_op[st + 4] == store && lp_a[st + 4] == v
      && lp_op[st + 5] == IR_DROP && lp_op[st + 6] == IR_DROP) {
    lp_fpost[d] = 1;            // v++ or v--
  } else if (nlp - st >= 2 && lp_op[nlp - 1] == IR_DROP
             && lp_op[nlp - 2] == store && lp_a[nlp - 2] == v) {
    lp_fpost[d] = 0;            // any step that ends storing v
  } else {
    return;
  }
  lp_fvar[d] = v;
  lp_fload[d] = (char)load;
}

// Does the held header of open loop d (body label lbody) have the shape
// of a counted loop? If so, set cl_var and the rest. The step is one
// that step_shape() knows; the test is
// LOCAL v and the limit (CONST or another LOCAL), in either order,
// compared signed (LT, LE, GT or GE: unsigned ones are ULT and the
// rest), then BRTRUE lbody. Counting up must end at < or <=, down at
// > or >=.
static int counted_shape(int d, int lbody)
{
  int t = lp_test[d], st = lp_step[d];
  if (!step_shape(d, &cl_var, &cl_step))
    return 0;
  if (st - t < 4 || lp_op[st - 1] != IR_BRTRUE || lp_a[st - 1] != lbody)
    return 0;
  cl_cmp = lp_op[st - 2];
  if (!IR_IS_RELATIONAL(cl_cmp))
    return 0;
  if (lp_op[t] == IR_LOCAL && lp_a[t] == cl_var) {
    cl_ls = t + 1;              // v, then the limit
    cl_le = st - 2;
  } else if (lp_op[st - 3] == IR_LOCAL && lp_a[st - 3] == cl_var) {
    cl_ls = t;                  // the limit, then v
    cl_le = st - 3;
    cl_cmp = IR_COMPARE_MIRROR(cl_cmp);
  } else {
    return 0;
  }
  cl_const = cl_le - cl_ls == 1 && lp_op[cl_ls] == IR_CONST;
  cl_lim = lp_a[cl_ls];
  if (!limit_ok(cl_ls, cl_le) || taken(cl_var) || cl_step == 0
      || (cl_step > 0) != (cl_cmp == IR_LT || cl_cmp == IR_LE))
    return 0;
  cl_nested = d > 0 || goto_seen;
  return 1;
}

// How IR op op, with no side effects, changes the stack: 1 for a push,
// 0 for a load through an address or a unary op, -1 for a binary op;
// or -2 if it isn't such an op.
static int pure_effect(int op)
{
  switch (op) {
  case IR_CONST: case IR_LOCAL: case IR_LOCALB: case IR_GLOBAL:
  case IR_GLOBALB: case IR_DATAADDR: case IR_STRADDR: case IR_HEAPADDR:
  case IR_FUNCADDR:
    return 1;
  case IR_LOAD: case IR_LOADB: case IR_NEG: case IR_COMP:
    return 0;
  case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
  case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
    return -1;
  }
  return -2;
}

// Is IR op op a push with no side effects, of one word?
static int pure_push(int op)
{
  return pure_effect(op) == 1;
}

// The init folded into a counted loop's count (init_fold()).
#define FOLD_MAX 12
static char fo_op[FOLD_MAX];
static int fo_a[FOLD_MAX], fo_f[FOLD_MAX];

// Is the swap buffer's tail an init of the counted loop's variable,
// v = e, with e an expression of no side effects (pure_effect(), which
// has no volatile load)? If so, copy e's ops
// to fo_op[] and the rest, and return how many; else 0. Walking back
// from the store, e starts where the ops met so far leave one value.
static int init_fold(void)
{
  int need = 1, i = nbuf - 3, n, e;
  if (nbuf < 3 || buf_op[nbuf - 1] != IR_DROP
      || buf_op[nbuf - 2] != IR_SETLOCAL || buf_a[nbuf - 2] != cl_var
      || buf_pre[nbuf - 2] || buf_pre[nbuf - 1])
    return 0;
  for (; i >= 0 && need > 0; i--) {
    if ((e = pure_effect(buf_op[i])) == -2 || buf_pre[i])
      return 0;
    need -= e;
  }
  n = nbuf - 3 - i;             // e is from i + 1
  if (need != 0 || n > FOLD_MAX
      || (i >= 0 && buf_op[i] == IR_SWAP))
    return 0;
  for (e = 0; e < n; e++) {
    fo_op[e] = buf_op[i + 1 + e];
    fo_a[e] = buf_a[i + 1 + e];
    fo_f[e] = buf_f[i + 1 + e];
  }
  return n;
}

// Is open loop d, which counted_shape() has just accepted, a candidate
// to end at != (ne_ok)? Its init, the swap buffer's tail
// (init_fold()), must store a constant into v that isn't past where v
// stops, and the stop must fit: v <= 32767 has none.
static void ne_candidate(int d)
{
  int stop = cl_lim, c;
  if (d)
    return;                     // an inner loop: the outer's stays
  if (goto_seen || !cl_const || (cl_step != 1 && cl_step != -1)
      || init_fold() != 1 || fo_op[0] != IR_CONST)
    return;
  if (cl_cmp == IR_LE || cl_cmp == IR_GE) {
    if (stop == (cl_step > 0 ? 0x7FFF : -0x8000))
      return;
    stop += cl_step;            // v <= n is v != n + 1
  }
  c = fo_a[0];
  if (cl_step > 0 ? c > stop : c < stop)
    return;
  ne_var = cl_var;
  ne_stop = stop;
  ne_ci = cl_ls;
  ne_cmp = lp_step[d] - 2;
  ne_ok = 1;
}

// Lower a counted loop's held body, from from up to (not including) to:
// before each RETURN, its result's words (its a) are stored in
// temporaries, the loop's limit and count dropped from under them, and
// the words pushed again. A one-word result that is one push just
// before the RETURN (return i;) is all of it, so the two are just
// dropped before that push.
static void replay_body(int from, int to)
{
  int w, k;
  for (; from < to; from++) {
    if (from + 1 < to && lp_op[from + 1] == IR_RETURN
        && lp_a[from + 1] == 1 && pure_push(lp_op[from])) {
      ir_hold(IR_DROP, 0, -1);
      ir_hold(IR_DROP, 0, -1);
      ir_hold(lp_op[from], lp_a[from], lp_f[from]);
      ir_hold(IR_RETURN, 1, -1);
      from++;
      continue;
    }
    if (lp_op[from] == IR_RETURN) {
      w = lp_a[from];
      for (k = w; k-- > 0;) {
        ir_hold(IR_SETLOCAL, FRAME_TEMP(k), -1);
        ir_hold(IR_DROP, 0, -1);
      }
      ir_hold(IR_DROP, 0, -1);
      ir_hold(IR_DROP, 0, -1);
      for (k = 0; k < w; k++)
        ir_hold(IR_LOCAL, FRAME_TEMP(k), -1);
    }
    ir_hold(lp_op[from], lp_a[from], lp_f[from]);
  }
}

// At LOOPEND, write the counted loop whose body is held (labels:
// lp_lbody[d] the top, lcont, lp_ltest[d] the test, lbrk the exit).
// The init just before the loop, still in the swap buffer, may be
// folded in (init_fold()): the value stored into the variable is
// pushed as the count instead (the loop always stores the variable
// itself). Then:
//   <limit> <count>  [entry]  top: DLW v  body  cont: <bottom>
//   exit: SLW v  DROP
// For v < n (and v > n), the bottom steps the count and tests it
// (INCR or ADDI k, BRLT top), and the entry jumps to the test; for
// v <= n, v < N (constant: the limit N - 1), and the same counting
// down, the bottom is INCBRLE (or CB k ADDBRLE, DECBRGE, CB k SUBBRGE)
// and the entry skips an empty loop (BRGT, or BRLT, exit). With both
// ends constant, the entry goes; if such a loop would never run, it is
// written as step 1 writes any loop. With both ends constant and an
// empty body, the loop does nothing a program may see but leave v its
// last value, so it goes, and the init stores that value instead
// (the guide, 4.10: a delay loop needs a volatile). Returns 0 if not
// written.
static int counted_loop(int d, int lcont, int lbrk)
{
  int up = cl_step > 0, k = up ? cl_step : -cl_step, lim = cl_lim;
  int fold = init_fold(), c = fo_a[0], sure = 0, bottom, i;
  int top = lp_lbody[d], ltest = lp_ltest[d];
  if (fold == 1 && fo_op[0] == IR_CONST && cl_const) {
    sure = cl_cmp == IR_LT ? c < lim : cl_cmp == IR_LE ? c <= lim
           : cl_cmp == IR_GT ? c > lim : c >= lim;
    if (!sure)
      return 0;                 // it never runs
    if (cl_body == nlp) {
      // Do nothing: v's last value is c plus k for each pass, d / k
      // of them for d the distance (rounded up for < and >; one more
      // for <= and >=), all 16 bits, as on the Apple II.
      unsigned dist = ((unsigned)(up ? lim - c : c - lim)) & 0xFFFF, n;
      n = dist / k + (cl_cmp == IR_LE || cl_cmp == IR_GE || dist % k != 0);
      n = (up ? (unsigned)c + n * k : (unsigned)c - n * k) & 0xFFFF;
      buf_a[nbuf - 3] = (int)((n ^ 0x8000) - 0x8000);
      label(lbrk);
      return 1;
    }
  }
  // The bottom: a step-and-branch op (INCBRLE ...), or a test (BRLT,
  // BRGT) after the step.
  if (cl_cmp == IR_LE || cl_cmp == IR_GE) {
    bottom = up ? (k == 1 ? OP_INCBRLE : OP_ADDBRLE)
                : (k == 1 ? OP_DECBRGE : OP_SUBBRGE);
  } else if (k == 1 && cl_const && lim != (up ? -0x8000 : 0x7FFF)) {
    bottom = up ? OP_INCBRLE : OP_DECBRGE;
    lim += up ? -1 : 1;         // v < N is v <= N - 1
  } else {
    bottom = up ? OP_BRLT : OP_BRGT;
  }
  if (fold)
    nbuf -= fold + 2;           // the init: its value is the count
  if (cl_const)
    ir_hold(IR_CONST, lim, -1);
  else
    replay(cl_ls, cl_le);       // the limit, computed once
  if (!fold)
    ir_hold(IR_LOCAL, cl_var, -1);
  for (i = 0; i < fold; i++)
    ir_hold(fo_op[i], fo_a[i], fo_f[i]);
  if (!sure) {
    if (bottom == OP_BRLT || bottom == OP_BRGT)
      jump(ltest);
    else
      mop_branch(up ? OP_BRGT : OP_BRLT, lbrk);   // an empty loop
  }
  lab_ref[top] = 1;
  ir_hold(IR_LABEL, top, -1);
  ir_hold(IR_SETLOCAL, cl_var, -1);       // DLW v
  replay_body(cl_body, nlp);
  label(lcont);
  if (bottom == OP_BRLT || bottom == OP_BRGT) {
    ir_hold(IR_CONST, k, -1);
    ir_hold(up ? IR_ADD : IR_SUB, 0, -1);    // INCR, ADDI k, ...
    label(ltest);
  } else if (k != 1) {
    ir_hold(IR_CONST, k, -1);
  }
  mop_branch(bottom, top);
  label(lbrk);
  ir_hold(IR_SETLOCAL, cl_var, -1);       // SLW v
  ir_hold(IR_DROP, 0, -1);
  ir_hold(IR_DROP, 0, -1);                // the limit
  return 1;
}

// Lay out a loop at its marker op, with labels a and f.
static void loop_op(int op, int a, int f)
{
  int d = lp_depth - 1, held;
  if (op == IR_LOOP) {
    if (lp_fill)
      ir_damaged();             // a loop in a header
    if (!lp_depth)
      ne_ok = 0;                // only its own ne_candidate() sets it
    d = lp_depth++;
    if (d < LOOP_NEST) {
      lp_held[d] = 1;
      lp_test[d] = nlp;
      lp_step[d] = -1;
      lp_ltest[d] = a;
      lp_fill = 1;
    } else {
      ir_hold(IR_LABEL, a, -1);
    }
    return;
  }
  if (d < 0)
    ir_damaged();               // a marker outside a loop
  held = d < LOOP_NEST && lp_held[d];
  if (held != (lp_fill == 1) && op != IR_LOOPEND)
    ir_damaged();               // a marker where it can't be
  if (op == IR_LOOPSTEP) {
    if (held) {
      lp_step[d] = nlp;
      lp_lcont[d] = a;
      lp_lbrk[d] = f;
    } else {
      jump(f);
      ir_hold(IR_LABEL, a, -1);
    }
  } else if (op == IR_LOOPBODY) {
    if (held) {
      lp_fill = 0;
      if (lp_step[d] < 0)
        ir_damaged();
      lp_lbody[d] = a;
      dlw_fix(d);
      if (counted_shape(d, a)) {
        ne_candidate(d);
        lp_fill = 2;            // hold the body too
        cl_body = nlp;
        return;
      }
      if (lp_step[d] - lp_test[d] != 1 || lp_op[lp_test[d]] != IR_JUMP
          || lp_a[lp_test[d]] != a)
        enter_test(d);          // enter at the test
      label(a);
    } else {
      jump(f);
      ir_hold(IR_LABEL, a, -1);
    }
  } else {
    if (lp_fill == 1)
      ir_damaged();
    if (lp_fill == 2 && counted_loop(d, a, f)) {
      lp_fill = 0;
      nlp = lp_test[d];
      lp_depth--;
      return;
    }
    if (lp_fill == 2)
      body_plain();
    if (d == 0 && ne_ok && !goto_seen) {
      lp_a[ne_ci] = ne_stop;    // v != stop: one BRNE
      lp_op[ne_cmp] = IR_NE;
    }
    if (held && lp_fvar[d] >= 0) {
      // The DLW fix: the step keeps v, which the test needn't push.
      label(a);
      if (lp_fpost[d]) {
        replay(lp_step[d], lp_step[d] + 1);     // without the DUP
        replay(lp_step[d] + 2, nlp - 2);        // and the DROPs
      } else {
        replay(lp_step[d], nlp - 1);    // all but its DROP
      }
      label(lp_ltest[d]);
      replay(lp_test[d] + 1, lp_step[d]);
      nlp = lp_test[d];
    } else if (held) {
      label(a);
      replay(lp_step[d], nlp);
      label(lp_ltest[d]);
      replay(lp_test[d], lp_step[d]);
      nlp = lp_test[d];
    } else {
      jump(a);
    }
    label(f);
    lp_depth--;
  }
}

// Take IR op op (with a and f) if a loop's layout needs it: a marker,
// or an op of a header or body being held. Returns 1 if taken; else the
// caller holds it. Every op is noted first: the labels branched to,
// and the frame offsets whose addresses are taken.
int loop_take(int op, int a, int f)
{
  int i;
  if (op == IR_FUNC) {
    for (i = 0; i < MAX_LABELS; i++)
      lab_ref[i] = 0;
    for (i = 0; i < 32; i++)
      addr_taken[i] = 0;
    goto_seen = 0;
    ne_ok = 0;
  } else if (op == IR_ENDFUNC && lp_depth) {
    ir_damaged();               // a loop not ended
  }
  if (op == IR_LABEL && IS_GOTO_LABEL(a))
    goto_seen = 1;
  // Anything in the body that may change v, a store or its address,
  // ends its candidacy. (v isn't volatile: counted_shape() takes only
  // a plain LOCAL, so its stores are plain too.)
  if ((op == IR_SETLOCAL || op == IR_SETLOCALB || op == IR_LOCALADDR)
      && near(a, ne_var))
    ne_ok = 0;
  if (IR_REFS_LABEL(op))
    lab_ref[a] = 1;
  else if (op == IR_JOIN)
    lab_ref[f] = 1;             // a's branches go to f
  if (op == IR_LOCALADDR && a >= 0 && a < 256)
    addr_taken[a >> 3] |= (unsigned char)(1 << (a & 7));
  if (lp_fill == 2 && (op == IR_LOOP || !body_ok(op, a, f)))
    body_plain();               // this loop can't be counted
  if (IR_IS_LOOP_MARKER(op))
    loop_op(op, a, f);
  else if (lp_fill)
    keep(op, a, f);
  else
    return 0;
  return 1;
}

// Start a file with no loop open.
void loop_reset(void)
{
  nlp = 0;
  lp_depth = 0;
  lp_fill = 0;
}

// Check ccgen's op-indexed tables against ir.h's op numbers: irin.c's
// stk_effect[], and lower.c's lw_kind[], lw_op[] and ir_names[]. They
// are written row by row in op order, which a _Static_assert can't
// check (a table's entry isn't a constant expression), so this test
// does, and ccgen itself carries no such code. It includes the two
// files, to see their static tables, and links with the rest of
// ccgen's sources (tables.sh). Every op's name is checked, since a
// name says which op it's for; every number that's no op must have no
// name and no stack effect; and the other tables are spot-checked at
// each block's first and last op, which a shifted or reordered block
// would break.
#include "irin.c"                // found through -Isrc
#include "lower.c"

static int bad;

// ccgen.c's, which this program replaces.
void clean_up(void)
{
}

// Report a failed check.
static void fail(const char *what, int op)
{
  printf("FAIL tables: %s, at op $%02X\n", what, op);
  bad = 1;
}

#define NAME(x)  do { if (!ir_names[IR_##x] \
                          || strcmp(ir_names[IR_##x], #x)) \
                        fail("ir_names[IR_" #x "] isn't \"" #x "\"", \
                             IR_##x); } while (0)
#define VNAME(x) do { int v_ = IR_FORM(IR_##x, IR_VOLATILE); \
                      if (!ir_names[v_] || strcmp(ir_names[v_], #x "V")) \
                        fail("ir_names[] of IR_" #x "'s volatile form isn't \"" \
                             #x "V\"", v_); } while (0)
#define STK(x, e) do { if (stk_effect[x] != (e)) \
                         fail("stk_effect[" #x "] isn't " #e, x); } while (0)
#define KIND(x, k) do { if (lw_kind[x] != (k)) \
                          fail("lw_kind[" #x "] isn't " #k, x); } while (0)
#define MOP(x, m) do { if (lw_op[x] != (m)) \
                         fail("lw_op[" #x "] isn't " #m, x); } while (0)

int main(void)
{
  int op, vol = IR_VOLATILE;

  // Every op's name, and its volatile form's.
  NAME(LOCAL); NAME(LOCALB); NAME(SETLOCAL); NAME(SETLOCALB);
  NAME(GLOBAL); NAME(GLOBALB); NAME(SETGLOBAL); NAME(SETGLOBALB);
  NAME(LOAD); NAME(LOADB); NAME(STORE); NAME(STOREB);
  NAME(LOCALADDR); NAME(DATAADDR); NAME(HEAPADDR); NAME(STRADDR);
  NAME(FUNC); NAME(ENDFUNC); NAME(CONST); NAME(CALL);
  NAME(DROP); NAME(DUP); NAME(RETURN); NAME(FUNCADDR);
  NAME(ICALL); NAME(NEG); NAME(COMP); NAME(NOT);
  NAME(SWAP); NAME(UDIV); NAME(UMOD); NAME(USHR);
  VNAME(LOCAL); VNAME(LOCALB); VNAME(SETLOCAL); VNAME(SETLOCALB);
  VNAME(GLOBAL); VNAME(GLOBALB); VNAME(SETGLOBAL); VNAME(SETGLOBALB);
  VNAME(LOAD); VNAME(LOADB); VNAME(STORE); VNAME(STOREB);
  NAME(ADD); NAME(SUB); NAME(MUL); NAME(DIV); NAME(MOD);
  NAME(AND); NAME(OR); NAME(XOR); NAME(SHL); NAME(SHR);
  NAME(EQ); NAME(NE); NAME(LT); NAME(GE); NAME(GT); NAME(LE);
  NAME(ULT); NAME(UGE); NAME(UGT); NAME(ULE);
  NAME(LABEL); NAME(JUMP); NAME(BRFALSE); NAME(BRTRUE); NAME(SEL);
  NAME(JOIN); NAME(CASES); NAME(CASEVAL); NAME(CASETO);
  NAME(LOOP); NAME(LOOPSTEP); NAME(LOOPBODY); NAME(LOOPEND);

  // A number that's no op has no name and no stack effect, and one
  // that's an op has both.
  for (op = 0; op < IR_NOPS; op++) {
    if (!IR_IS_OP(op) != !ir_names[op])
      fail(IR_IS_OP(op) ? "an op has no name" : "a number that's no op has"
                                                " a name", op);
    if (!IR_IS_OP(op) && stk_effect[op] != STK_NONE)
      fail("a number that's no op has a stack effect", op);
  }

  // Spot checks: each block's first and last op, and a volatile form.
  STK(IR_LOCAL, 0x01); STK(IR_STOREB, 0x20); STK(IR_LOCALADDR, 0x01);
  STK(IR_STRADDR, 0x01); STK(IR_FUNC, STK_NONE); STK(IR_USHR, 0x21);
  STK(IR_LOCAL | vol, 0x01); STK(IR_STOREB | vol, 0x20);
  STK(IR_ADD, 0x21); STK(IR_SHR, 0x21); STK(IR_EQ, 0x21);
  STK(IR_ULE, 0x21); STK(IR_LABEL, 0x00); STK(IR_CASETO, STK_NONE);
  STK(IR_LOOP, STK_NONE); STK(IR_LOOPEND, STK_NONE);
  KIND(IR_LOCAL, L_OPERAND); KIND(IR_STOREB, L_SIMPLE);
  KIND(IR_LOCALADDR, L_OPERAND); KIND(IR_STRADDR, L_SPECIAL);
  KIND(IR_FUNC, L_SPECIAL); KIND(IR_USHR, L_SPECIAL);
  KIND(IR_LOCAL | vol, L_OPERAND); KIND(IR_STOREB | vol, L_SIMPLE);
  KIND(IR_ADD, L_SIMPLE); KIND(IR_SHR, L_SIMPLE); KIND(IR_EQ, L_COMPARE);
  KIND(IR_ULE, L_COMPARE); KIND(IR_LABEL, L_OPERAND);
  KIND(IR_CASETO, L_OPERAND); KIND(IR_LOOP, L_SPECIAL);
  KIND(IR_LOOPEND, L_SPECIAL);
  MOP(IR_LOCAL, OP_LLW); MOP(IR_STOREB, OP_SB); MOP(IR_LOCALADDR, OP_LLA);
  MOP(IR_DROP, OP_DROP); MOP(IR_ICALL, OP_ICAL);
  MOP(IR_LOCAL | vol, OP_LLW | MOP_VOLATILE);
  MOP(IR_STOREB | vol, OP_SB | MOP_VOLATILE);
  MOP(IR_ADD, OP_ADD); MOP(IR_SHR, OP_SHR); MOP(IR_EQ, OP_ISEQ);
  MOP(IR_ULE, OP_ISLE); MOP(IR_LABEL, OP_LABEL); MOP(IR_CASETO, OP_CASETO);
  return bad;
}

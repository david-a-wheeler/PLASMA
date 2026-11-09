// ccgen's loop layout (irloop.c), and what it shares with irin.c: the
// swap buffer, which a counted loop's init is folded out of, and the
// calls that hold ops for lowering. Only irin.c and irloop.c include it.
#ifndef IRLOOP_H
#define IRLOOP_H

#define IR_BUF 128
extern char buf_op[IR_BUF];         // the swap buffer (irin.c)
extern int buf_a[IR_BUF];
extern int buf_f[IR_BUF];
extern char buf_pre[IR_BUF];
extern int nbuf;

void ir_hold(int op, int a, int f);     // hold an op for lowering
void ir_damaged(void);                  // stop: the IR file is damaged
int ir_mirror(int cmp);         // the comparison, operands exchanged

int loop_take(int op, int a, int f);    // 1 if a loop's layout took it
void loop_reset(void);                  // before each file

#endif

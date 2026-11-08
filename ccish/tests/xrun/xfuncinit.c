/* xfuncinit.c: function addresses in initializers. A global pointer, a
 * table of them, a struct of them (the shape of PLASMA's FILEIO table),
 * a static local, a const table, a function defined after the table
 * that names it, and a local array and struct, whose initializers are
 * code. */
#include "ptest.h"

typedef int binop(int, int);

int add(int a, int b)
{
  return a + b;
}

int sub(int a, int b)
{
  return a - b;
}

static int mul(int a, int b)
{
  return a * b;
}

int later(int a, int b);

struct io
{
  int (*open)(int, int);
  int (*close)(int, int);
  int tag;
  binop *spare;
};

binop *gp = add;
int (*gq)(int, int) = &sub;
binop *table[] = {add, sub, mul, later, 0};
binop *const ctable[2] = {&mul, later};
struct io dev = {add, &mul, 7, 0};
struct io dev2 = {later, sub, 2, mul};

/* Defined after the tables that name it. */
int later(int a, int b)
{
  return 100 * a + b;
}

/* A static local's initializer is data too: count the calls. */
int counted(int a)
{
  static binop *op = add;
  static int n;
  n++;
  return op(a, n);
}

int main(void)
{
  binop *ops[3] = {sub, gp, mul};
  struct io loc = {mul, later, 9, add};
  int i, s;

  puti(gp(2, 3)); putc(' ');
  puti(gq(2, 3)); putc(' ');
  puti(gp == add); putc(' ');
  puti(gq == sub); putln();

  s = 0;
  for (i = 0; table[i]; i++) {
    puti(table[i](6, 3)); putc(' ');
    s += i;
  }
  puti(s); putln();

  puti(ctable[0](4, 5)); putc(' ');
  puti(ctable[1](4, 5)); putln();

  puti(dev.open(10, 1)); putc(' ');
  puti(dev.close(10, 2)); putc(' ');
  puti(dev.tag); putc(' ');
  puti(dev.spare == 0); putln();

  puti(dev2.open(3, 4)); putc(' ');
  puti(dev2.close(3, 4)); putc(' ');
  puti(dev2.tag); putc(' ');
  puti(dev2.spare(5, 6)); putln();

  puti(counted(10)); putc(' ');
  puti(counted(10)); putc(' ');
  puti(counted(10)); putln();

  for (i = 0; i < 3; i++) {
    puti(ops[i](8, 2)); putc(' ');
  }
  puti(loc.open(3, 3)); putc(' ');
  puti(loc.close(3, 3)); putc(' ');
  puti(loc.tag); putc(' ');
  puti(loc.spare(3, 3)); putln();
  return 0;
}

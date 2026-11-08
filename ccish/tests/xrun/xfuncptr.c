/* xfuncptr.c: function pointers. Taking a function's address, calling
 * through a pointer (in a variable, an array, a struct, a parameter,
 * a call's result), comparing pointers, and the forms that call a named
 * function directly ((*f)(x), (&f)(x)). Global initializers come later
 * (step 3), so tables are filled in at run time. */
#include "ptest.h"

typedef int binop(int, int);

struct pt
{
  int x;
  int y;
};

struct ops
{
  binop *op;
  int (*un)(int);
  char name;
};

int calls;

int add(int a, int b)
{
  calls++;
  return a + b;
}

int sub(int a, int b)
{
  calls++;
  return a - b;
}

int mul(int a, int b)
{
  calls++;
  return a * b;
}

int twice(int a)
{
  return 2 * a;
}

int neg(int a)
{
  return -a;
}

char letter(int k)
{
  return (char)('a' + k);
}

void show(int v)
{
  puti(v);
  putc(' ');
}

struct pt mkpt(int x, int y)
{
  struct pt p;
  p.x = x;
  p.y = y;
  return p;
}

static int fact(int n);

/* Set at run time to fact, which calls itself through it. */
static int (*factp)(int);

static int fact(int n)
{
  return n <= 1 ? 1 : n * factp(n - 1);
}

binop *table[3];
int (*gun)(int);

/* A pointer as a parameter: call it. */
int apply(binop *f, int a, int b)
{
  return f(a, b);
}

/* The same, through the explicit dereference. */
int apply2(int (*f)(int), int a)
{
  return (*f)(a);
}

/* A function that returns a function pointer. */
binop *pick(int k)
{
  return table[k];
}

/* Its declarator form, without the typedef. */
int (*pick_un(int k))(int)
{
  return k ? neg : twice;
}

int main(void)
{
  int (*fp)(int);
  int (**pp)(int);
  void (*vp)(int);
  char (*cp)(int);
  struct pt (*mp)(int, int);
  struct pt q;
  struct ops o, *op;
  binop *ops[3];
  int i, s;

  /* A variable: f, &f, and calls through it. */
  fp = twice;
  puti(fp(5)); putc(' ');
  puti((*fp)(6)); putc(' ');
  fp = &neg;
  puti(fp(7)); putc(' ');
  puti((**fp)(8)); putc(' ');
  puti((&*fp)(9)); putc(' ');
  puti(&*fp == fp); putln();

  /* Named functions called through the forms that name them. */
  puti((*twice)(9)); putc(' ');
  puti((&twice)(10)); putc(' ');
  puti((***neg)(11)); putln();

  /* A global pointer, and one to a pointer. */
  gun = twice;
  pp = &gun;
  puti(gun(12)); putc(' ');
  puti((*pp)(13)); putc(' ');
  *pp = neg;
  puti(gun(14)); putln();

  /* Arrays of pointers: a global one, a local one, a variable index,
   * in a counted loop. */
  table[0] = add;
  table[1] = sub;
  table[2] = mul;
  ops[0] = mul;
  ops[1] = add;
  ops[2] = sub;
  s = 0;
  for (i = 0; i < 3; i++)
    s += table[i](10, i + 1) + ops[i](i, 3);
  puti(s); putc(' ');
  puti(table[2](table[0](1, 2), table[1](9, 4))); putc(' ');
  puti(calls); putln();

  /* Members, through a struct and a pointer to it. */
  o.op = sub;
  o.un = neg;
  o.name = 'o';
  op = &o;
  puti(o.op(20, 5)); putc(' ');
  puti(op->op(3, 30)); putc(' ');
  puti(op->un(op->op(1, 2))); putc(' ');
  putc(o.name); putln();

  /* Pointers passed and returned. */
  puti(apply(mul, 6, 7)); putc(' ');
  puti(apply(&add, 6, 7)); putc(' ');
  puti(apply2(twice, 21)); putc(' ');
  puti(pick(1)(50, 8)); putc(' ');
  puti(pick_un(1)(4)); putc(' ');
  puti((*pick_un(0))(4)); putln();

  /* A choice of function, then a call of the result. */
  for (i = 0; i < 2; i++) {
    puti((i ? add : sub)(9, 2)); putc(' ');
  }
  putln();

  /* Comparisons. */
  fp = twice;
  puti(fp == twice); putc(' ');
  puti(fp == neg); putc(' ');
  puti(fp != 0); putc(' ');
  fp = 0;
  puti(!fp); putc(' ');
  puti(table[0] == add && table[1] != add); putln();

  /* void, char and struct results. */
  vp = show;
  vp(1);
  (*vp)(2);
  cp = letter;
  putc(cp(3));
  putc((*cp)(25));
  putln();
  mp = mkpt;
  q = mp(4, 5);
  puti(q.x * 10 + q.y); putc(' ');
  puti(mp(6, 7).y); putln();

  /* Recursion through a pointer. */
  factp = fact;
  puti(factp(7)); putln();

  /* A CMDSYS function, by address. */
  vp = puti;
  vp(321);
  putln();
  return 0;
}

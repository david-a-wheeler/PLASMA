// genfuncptr.c - calls through function pointers (approach.md,
// "Function pointer design"): ICAL takes the address on top of the
// arguments, so a pointer in a variable or an element is loaded after
// them, with no temporary, and a call's result is kept in one; a named
// function called as (*f)() or (&f)() is a plain CALL; a function's
// address is an LA; a void call through a pointer leaves nothing to
// drop, and an int one's unused result is dropped.
typedef int binop(int, int);
binop *table[2];

int add(int a, int b) { return a + b; }
void nop(int a) { }
binop *pick(int k) { return table[k]; }
int param(int (*fp)(int), int x) { return fp(x) + (*fp)(x); }
int elem(int i) { return table[i](i, 2); }
int named(void) { return (*add)(1, 2) + (&add)(3, 4); }
int result(void) { return pick(1)(5, 6); }
int fixed(void) { return ((int (*)(int))0x300)(7); }
void stmt(void (*v)(int), binop *b) { v(1); b(2, 3); }

int main(void)
{
  table[0] = add;
  table[1] = &add;
  stmt(nop, add);
  return elem(1) + result() - named() - 4;
}

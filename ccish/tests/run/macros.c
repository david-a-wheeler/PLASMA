// Object-like macros, #include (twice, behind an include guard),
// #ifdef, #ifndef, #else, #undef and a redefinition with the same body
// (approach.md, "ccpp design", step 4).
#include "inc/macros.h"
#include "inc/macros.h"
#define SIZE 3
#ifdef SIZE
int a[SIZE];
#else
int a[1];
#endif
#ifndef GREETING
#error not reached: a skipped group's directives don't count
#endif
#define LOOP for (i = 0; i < SIZE; i++)
int main()
{
  int i;
  LOOP a[i] = i * 10;
  __cmd_puti(TWICE_SIZE * 2); __cmd_putc(' ');      // 3 + 3 * 2: no parentheses
  __cmd_puti(a[2]); __cmd_putc(' ');
  __cmd_puts(pstr(GREETING " there"));
  __cmd_putln();
#undef SIZE
#define SIZE 7
  __cmd_puti(SIZE);
  __cmd_putln();
  return 0;
}

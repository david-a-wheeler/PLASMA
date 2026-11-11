// undef_redefine.c - whether a name is a macro must follow the macro
// table (ppmac.c), which the #undef compacts and the #define refills,
// not a copy of the flag: the printed intermediates pin defined(M) as
// 1, then 0, then 1 again (approach.md, "ccpp's name lookup").
#include "ptest.h"

#define M 4
int main(void)
{
  int n = 0;
#if defined(M)
  n = 1;
#endif
  puti(n);
  putc(' ');
#undef M
#if defined(M)
  n = 2;
#else
  n = n + 3;
#endif
  puti(n);
  putc(' ');
#define M 8
#if defined(M)
  n = n + M;
#endif
  puti(n);
  putc(' ');
#ifdef M
  n = n * 2;
#endif
  puti(n);
  putln();
  return 0;
}

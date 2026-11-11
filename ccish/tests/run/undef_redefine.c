// #undef and #define again: whether a name is a macro must follow the
// macro table (ppmac.c), which the #undef compacts and the #define
// refills, and not a copy of the flag: defined(M) is 1, then 0, then 1
// again (approach.md, "ccpp's name lookup").
#define M 4
int main(void)
{
  int n = 0;
#if defined(M)
  n = 1;
#endif
#undef M
#if defined(M)
  n = 2;
#else
  n = n + 3;
#endif
#define M 8
#if defined(M)
  n = n + M;
#endif
#ifdef M
  n = n * 2;
#endif
  __puti(n);
  __putln();
  return 0;
}

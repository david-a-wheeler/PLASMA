// A CMDSYS function used as a value (pexpr.c, "declared by use"): the
// first mention of its name without a call declares it, as the call
// would, and the name is a function designator, a pointer to the
// function. Calling through the pointer reaches the CMDSYS function
// (putc's letter and puti's number print differently, so a wrong
// function or a wrong signature shows), and the addresses compare as
// C's function pointers do: equal to themselves, unequal across
// functions.
int main(void)
{
  void (*fp)(char) = __cmd_putc;
  void (*fpi)(int) = __cmd_puti;
  fp('A');
  fpi(7);
  __cmd_putc(' ');
  __cmd_puti(fp == fpi);
  __cmd_putc(' ');
  __cmd_puti(fp == __cmd_putc);
  __cmd_putln();
  return 0;
}

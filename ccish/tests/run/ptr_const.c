// A constant pointer (a cast integer) plus or minus a constant folds
// (pexpr.c, fold_held()), so it is an address constant in a global's
// initializer, scaled by the element size, as lores.c's screen rows
// need ((char *)0x400 + 128). A difference of two such pointers is
// still a subtraction, and a constant pointer plus a variable is code.
char *const rows[3] = { (char *)0x400, (char *)0x400 + 128,
                        (char *)0x400 + 2 * 128 + 40 };
int *const iw = (int *)0x1000 + 3;
char *const back = (char *)0x400 - 1;

int main()
{
  int i = 5;
  __cmd_puth((int)rows[0]); __cmd_putc(' ');
  __cmd_puth((int)rows[1]); __cmd_putc(' ');
  __cmd_puth((int)rows[2]); __cmd_putc(' ');
  __cmd_puth((int)iw); __cmd_putc(' ');
  __cmd_puth((int)back); __cmd_putln();
  __cmd_puti((char *)0x500 - (char *)0x400); __cmd_putc(' ');
  __cmd_puth((int)((char *)0x400 + i)); __cmd_putc(' ');
  __cmd_puth((int)((int *)0x400 + i)); __cmd_putln();
  return 0;
}

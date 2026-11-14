// A comma operator costs nothing: i = 0, j = 3; is the same ops as
// the two statements, in both functions here (the left assignment's
// kept value is dropped, and the peepholes turn the store-and-keep
// plus DROP into a plain store; doc/add-comma.md). The counts pin
// that: one CN 0 and one store to each local per function, and no
// DROP anywhere.
// listing-count: 2 CN 0
// listing-count: 2 SLW 0
// listing-count: 2 SLW 2
// listing-lacks: DROP
int sep(void)
{
  int i, j;
  i = 0;
  j = 3;
  return i + j;
}

int com(void)
{
  int i, j;
  i = 0, j = 3;
  return i + j;
}

int main(void)
{
  __cmd_puti(sep());
  __cmd_putc(' ');
  __cmd_puti(com());
  __cmd_putln();
  return 1;
}

// A betweenness test on a char local reads it twice, as written, if
// it is volatile; the plain twin's two loads become one, masked with
// the block's mask (irin.c, between_rewrite: '0' to '7' is a block of
// 8, so ANDI 248), and that one load then merges with the store before
// it (SLB LLB is DLB, which a volatile store isn't: peep.c). So vbet's
// SLB is followed by 2 LLBs, and pbet has none.
// listing-count: 2 LLB
// listing-count: 1 SLB
// listing-count: 1 ANDI 248
int vbet(int k)
{
  volatile char c = (char)k;
  if (c >= '0' && c <= '7')
    return 1;
  return 0;
}

int pbet(int k)
{
  char c = (char)k;
  if (c >= '0' && c <= '7')
    return 1;
  return 0;
}

int main()
{
  __cmd_puti(vbet('5'));
  __cmd_putc(' ');
  __cmd_puti(pbet('5'));
  __cmd_putc(' ');
  __cmd_puti(vbet('9'));
  __cmd_putc(' ');
  __cmd_puti(pbet('9'));
  __cmd_putln();
  return 0;
}

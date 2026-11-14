// Only CMDSYS may define a __cmd_ name (C11 7.1.3 reserves __ names to
// the implementation): defining one, even with its own type, is a link
// error, not a replacement for the import (doc/linking.md, 3.2).
void __cmd_puti(int n)
{
  __cmd_putc((char)(48 + n));
}

int main(void)
{
  __cmd_puti(7);
  return 0;
}

// The other half of main_second: see a.c.
// listing-lacks: upcase (import)
// listing-has: ; upcase
int upcase(int c);
int bump();
extern int count;

int main()
{
  __cmd_putc(upcase('a')); __cmd_putc(' ');
  __cmd_puti(bump(5)); __cmd_putc(' ');
  __cmd_puti(count);
  __cmd_putln();
  return 0;
}

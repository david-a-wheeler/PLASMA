// The other half of main_second: see a.c.
// listing-lacks: __toupper (import)
// listing-has: ; __toupper
int __toupper(int c);
int bump();
extern int count;

int main()
{
  __putc(__toupper('a')); __putc(' ');
  __puti(bump(5)); __putc(' ');
  __puti(count);
  __putln();
  return 0;
}

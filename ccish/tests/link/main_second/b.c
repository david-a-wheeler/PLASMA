/* The other half of main_second: see a.c. */
// listing-lacks: toupper (import)
// listing-has: ; toupper
int toupper(int c);
int bump();
extern int count;

int main()
{
  putc(toupper('a')); putc(' ');
  puti(bump(5)); putc(' ');
  puti(count);
  putln();
  return 0;
}

// Struct copies, compound assignment and ++ on globals declared extern
// and defined after main, so that each goes through a data symbol
// relocation (approach.md issue 11).
struct pt { int x, y; };
extern struct pt o;
extern char buf[];
extern int n;
int main()
{
  struct pt l;
  l = o;
  l.x += 10;
  o = l;
  n++;
  n *= 3;
  buf[n] = 'z';
  buf[0] += 1;
  __cmd_puti(o.x); __cmd_putc(' '); __cmd_puti(o.y); __cmd_putc(' '); __cmd_puti(n); __cmd_putc(' ');
  __cmd_putc(buf[0]); __cmd_putc(buf[6]); __cmd_putln();
  return 0;
}
struct pt o = {1, 2};
char buf[8] = "abc";
int n = 1;

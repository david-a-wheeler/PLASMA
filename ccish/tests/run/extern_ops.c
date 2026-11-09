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
  __puti(o.x); __putc(' '); __puti(o.y); __putc(' '); __puti(n); __putc(' ');
  __putc(buf[0]); __putc(buf[6]); __putln();
  return 0;
}
struct pt o = {1, 2};
char buf[8] = "abc";
int n = 1;

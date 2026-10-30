/* The code for globals: loads and stores use the absolute-address ops
 * (LAW, SAW, DAW, SAB), with no temporaries, so the frame holds only i.
 * An unused g++ keeps no copy (no DUP), a constant index is a fixed data
 * offset (a[3] is data+10: g, h, then a at 4), and a[i] = g needs no
 * temporary. */
// listing-has: ENTER 2,0
// listing-has: DAW
// listing-has: SAB
// listing-has: ; data+10
// listing-lacks: DUP
// listing-count: 1 IDXW
int g, h, a[4];
char c;

int main()
{
  int i = 2;
  g = 5;
  h = g = 7;
  g++;
  a[i] = g;
  a[3] = 1;
  c = 'A';
  puti(a[2] + a[3] + h);
  putc(' ');
  putc(c);
  putln();
  return 0;
}

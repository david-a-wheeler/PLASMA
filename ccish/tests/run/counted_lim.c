// Counted loops with limits that aren't a constant or a local
// (approach.md, "Possible improvements to the loop forms", 2): a
// global, a char global, an expression of a local, array elements; the
// limit first, with a store to another global in the body; <= with an
// expression; and inits that are expressions, folded into the count. Each limit is computed once, on entry, which is
// right only because nothing in the body can change it
// (counted_not.c has the loops whose bodies could).
// The inits that are expressions (i = at[m], i = n - 1, i = at[m] + 1)
// are folded into the count, so i is stored only at each loop's exit:
// 8 SLWs of i ([0]), one per loop.
// listing-count: 6 BRLT
// listing-count: 1 ADDBRLE
// listing-count: 2 BRGT
// listing-count: 1 DECBRGE
// listing-count: 8 SLW 0
int nf = 4;
char nb = 3;
int at[3] = {2, 5, 7};
int len[3] = {3, 2, 4};
int other;

void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int main()
{
  int i, m = 1, n = 6, s;
  s = 0;
  for (i = 0; i < nf; i++)      // a global
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < nb; i++)      // a char global
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < n - 1; i++)   // an expression of a local
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = at[m]; i < at[m] + len[m]; i++)  // array elements
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 0; nf > i; i++) {    // the limit first; another global
    s += i;
    other = s;
  }
  show(s);
  show(other);
  show(i);
  s = 0;
  for (i = 0; i <= nf * 2; i += 3)  // <=, an expression: ADDBRLE
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = n - 1; i >= 0; i--)  // an init that is an expression
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = at[m] + 1; i > nb; i--)  // one that loads
    s += i;
  show(s);
  show(i);
  __cmd_putln();
  return 0;
}

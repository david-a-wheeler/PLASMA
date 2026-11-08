/* if and else: else-if chains, which the parser reads as a loop that
 * shares one end label (pstmt.c, parse_statement), with and without a
 * final else; a dangling else inside a chain's branch; an if nested in
 * a branch; and a chain of 40, longer than any nesting limit. */

/* A chain with a final else: 1-5 by range, else 9. */
int classify(int n)
{
  int r;
  if (n < 0)
    r = 1;
  else if (n == 0)
    r = 2;
  else if (n < 10)
    r = 3;
  else if (n < 100)
    r = 4;
  else if (n < 1000)
    r = 5;
  else
    r = 9;
  return r;
}

/* A chain without a final else: r stays 0 when nothing matches. */
int partial(int n)
{
  int r = 0;
  if (n == 1)
    r = 10;
  else if (n == 2)
    r = 20;
  else if (n == 3)
    r = 30;
  return r;
}

/* An if nested in a chain's branch, whose else is its own (the
 * dangling else binds to the nearest if), then the chain goes on. */
int nested(int a, int b)
{
  int r;
  if (a == 1)
    if (b == 1)
      r = 11;
    else
      r = 12;
  else if (a == 2) {
    if (b == 1)
      r = 21;
    r = r + 100;
  } else
    r = 3;
  return r;
}

/* A chain of 40 branches: n's value, or -1. One return, since a
 * function may have at most 32 (MAX_RETURNS). */
int longchain(int n)
{
  int r = -1;
  if (n == 0) r = 0; else if (n == 1) r = 1;
  else if (n == 2) r = 2; else if (n == 3) r = 3;
  else if (n == 4) r = 4; else if (n == 5) r = 5;
  else if (n == 6) r = 6; else if (n == 7) r = 7;
  else if (n == 8) r = 8; else if (n == 9) r = 9;
  else if (n == 10) r = 10; else if (n == 11) r = 11;
  else if (n == 12) r = 12; else if (n == 13) r = 13;
  else if (n == 14) r = 14; else if (n == 15) r = 15;
  else if (n == 16) r = 16; else if (n == 17) r = 17;
  else if (n == 18) r = 18; else if (n == 19) r = 19;
  else if (n == 20) r = 20; else if (n == 21) r = 21;
  else if (n == 22) r = 22; else if (n == 23) r = 23;
  else if (n == 24) r = 24; else if (n == 25) r = 25;
  else if (n == 26) r = 26; else if (n == 27) r = 27;
  else if (n == 28) r = 28; else if (n == 29) r = 29;
  else if (n == 30) r = 30; else if (n == 31) r = 31;
  else if (n == 32) r = 32; else if (n == 33) r = 33;
  else if (n == 34) r = 34; else if (n == 35) r = 35;
  else if (n == 36) r = 36; else if (n == 37) r = 37;
  else if (n == 38) r = 38; else if (n == 39) r = 39;
  return r;
}

int main()
{
  __puti(classify(-5)); __putc(' ');
  __puti(classify(0)); __putc(' ');
  __puti(classify(7)); __putc(' ');
  __puti(classify(42)); __putc(' ');
  __puti(classify(999)); __putc(' ');
  __puti(classify(5000)); __putln();
  __puti(partial(1)); __putc(' ');
  __puti(partial(2)); __putc(' ');
  __puti(partial(3)); __putc(' ');
  __puti(partial(4)); __putln();
  __puti(nested(1, 1)); __putc(' ');
  __puti(nested(1, 2)); __putc(' ');
  __puti(nested(2, 1)); __putc(' ');
  __puti(nested(3, 0)); __putln();
  __puti(longchain(0)); __putc(' ');
  __puti(longchain(20)); __putc(' ');
  __puti(longchain(39)); __putc(' ');
  __puti(longchain(40)); __putln();
  return 0;
}

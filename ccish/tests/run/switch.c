/* switch (M7): PLASMA's SEL with a sorted case table after the body.
 * Cases in any order (negative ones too), grouped cases, fall through,
 * a default anywhere or none, nested switches, break and continue in a
 * loop, return from a case, case labels inside a nested block (Duff's
 * device), constant expressions as case values, and switches with no
 * cases. */
// listing-has: SEL
// listing-has: CASES 5
// listing-lacks: ISEQ

/* Classify a character. */
int kind(int c)
{
  switch (c) {
  case 'z': case 'a':
    return 1;
  case '0':
    return 2;
  default:
    return 0;
  case ' ': case '\n':
    return 3;
  }
}

/* Name a sign, with fall through and negative cases. */
void sign(int v)
{
  switch (v) {
  case -1:
    __putc('-');
    /* fall through */
  case 0:
    __putc('0');
    break;
  case 1:
    __putc('+');
    break;
  case 30000:
    __putc('B');
    break;
  }
  __putc(' ');
}

/* Copy n words with Duff's device: case labels inside a loop. */
void copy(int *to, int *from, int n)
{
  int k = (n + 3) / 4;
  switch (n % 4) {
  case 0: do { *to++ = *from++;
  case 3:      *to++ = *from++;
  case 2:      *to++ = *from++;
  case 1:      *to++ = *from++;
      } while (--k > 0);
  }
}

int main()
{
  int i, a[7] = {1, 2, 3, 4, 5, 6, 7}, b[7] = {0};
  char c = 'q';

  __puti(kind('a')); __puti(kind('z')); __puti(kind('0'));
  __puti(kind(' ')); __puti(kind('\n')); __puti(kind('?')); __putln();

  for (i = -2; i <= 2; i++)
    sign(i);
  sign(30000);
  __putln();

  /* nested switches, and break and continue in a loop */
  for (i = 0; i < 6; i++) {
    switch (i % 3) {
    case 0:
      switch (i) {
      case 0: __putc('A'); break;
      default: __putc('B');
      }
      __putc('.');
      break;
    case 1:
      continue;               /* the loop's next round */
    case 2 + sizeof(int) - 2:   /* a constant expression: 2 */
      __putc('C');
    }
    __putc(';');
  }
  __putln();

  copy(b, a, 7);
  for (i = 0; i < 7; i++)
    __puti(b[i]);
  __putc(' ');

  switch (c) {                    /* a char value */
  case 'q': __puts(pstr("q ")); break;
  case 'Q': __puts(pstr("Q ")); break;
  }
  switch (i) { }                  /* no cases */
  switch (i) { default: __putc('D'); }
  switch (kind('a') + 1) { case 2: __putc('2'); }
  __putln();
  return 0;
}

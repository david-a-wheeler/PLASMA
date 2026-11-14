// switch (M7): PLASMA's SEL with a sorted case table after the body.
// Cases in any order (negative ones too), grouped cases, fall through,
// a default anywhere or none, nested switches, break and continue in a
// loop, return from a case, case labels inside a nested block (Duff's
// device), constant expressions as case values, and switches with no
// cases.
// listing-has: SEL
// listing-has: CASES 5
// listing-lacks: ISEQ

// Classify a character.
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

// Name a sign, with fall through and negative cases.
void sign(int v)
{
  switch (v) {
  case -1:
    __cmd_putc('-');
    // fall through
  case 0:
    __cmd_putc('0');
    break;
  case 1:
    __cmd_putc('+');
    break;
  case 30000:
    __cmd_putc('B');
    break;
  }
  __cmd_putc(' ');
}

// Copy n words with Duff's device: case labels inside a loop.
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

  __cmd_puti(kind('a')); __cmd_puti(kind('z')); __cmd_puti(kind('0'));
  __cmd_puti(kind(' ')); __cmd_puti(kind('\n')); __cmd_puti(kind('?')); __cmd_putln();

  for (i = -2; i <= 2; i++)
    sign(i);
  sign(30000);
  __cmd_putln();

  // nested switches, and break and continue in a loop
  for (i = 0; i < 6; i++) {
    switch (i % 3) {
    case 0:
      switch (i) {
      case 0: __cmd_putc('A'); break;
      default: __cmd_putc('B');
      }
      __cmd_putc('.');
      break;
    case 1:
      continue;               // the loop's next round
    case 2 + sizeof(int) - 2:   // a constant expression: 2
      __cmd_putc('C');
    }
    __cmd_putc(';');
  }
  __cmd_putln();

  copy(b, a, 7);
  for (i = 0; i < 7; i++)
    __cmd_puti(b[i]);
  __cmd_putc(' ');

  switch (c) {                    // a char value
  case 'q': __cmd_puts(pstr("q ")); break;
  case 'Q': __cmd_puts(pstr("Q ")); break;
  }
  switch (i) { }                  // no cases
  switch (i) { default: __cmd_putc('D'); }
  switch (kind('a') + 1) { case 2: __cmd_putc('2'); }
  __cmd_putln();
  return 0;
}

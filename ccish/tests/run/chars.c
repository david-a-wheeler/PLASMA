// char is one byte and unsigned. A value is converted to char (its low
// byte kept) when it is stored into a char, passed as a char parameter,
// returned from a char function, or cast to char. Casts to int and void
// work too.
// listing-has: LLB
// listing-has: DLB
// listing-has: SLB
// listing-has: ANDI 255
// listing-count: 1 ENTER 4,0
void show(int n)
{
  __puti(n);
  __putc(' ');
}

void param(char c)
{
  show(c);
}

char low(int x)
{
  return x;
}

char konst()
{
  return 0x1234;
}

int is_a(char c)
{
  return c == 'A';
}

// Two chars take one byte each, so the frame is 4 bytes.
int frame()
{
  char a = 'A', b = 'B';
  int n = 1000;
  a++;
  return a + b + n;
}

int main()
{
  char c = 300;
  int i, count;

  show(c);
  show(c = 511);
  show(c + 1);
  show(++c);
  show(c++);
  show(c);
  c = 0;
  show(c--);
  show(c);
  c = 250;
  show(c += 10);
  __putln();

  c = -1;
  show(c);
  show(c > 0);
  show(-c);
  __putln();

  param(300);
  param(-1);
  show(low(321));
  show(low(-2));
  show(konst());
  show(is_a('A' + 256));
  __putln();

  i = -1;
  show((char)300);
  show((char)i);
  show((int)c);
  c = 255;
  show((char)(c + 1));
  show((char)i + 1);
  (void)low(1);
  (void)i;
  __putln();

  count = 0;
  for (c = 250; c != 4; c++)
    count++;
  show(count);
  show(frame());
  __putc(c + 'a');
  __putln();
  return 0;
}

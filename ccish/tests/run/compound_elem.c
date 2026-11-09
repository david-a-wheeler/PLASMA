// a[i] op= e saves the element's address in a temporary, so it is
// computed once (approach.md, M4 step 3 notes): one IDXLW (index op) per compound
// assignment, and the temporary is reused (DLW). In a[i] += f(&i), f
// changes i, but the store still goes to the element that was loaded.
// C lets f run before or after a[i] is found; ccish finds a[i] first.
// listing-count: 3 IDXLW
// listing-has: DLW
int f(int *q)
{
  *q = 2;
  return 10;
}

int main()
{
  int a[3], i;
  char s[4], c;

  a[1] = 1;
  a[2] = 2;
  i = 1;
  a[i] += f(&i);
  __puti(a[1]);
  __putc(' ');
  __puti(a[2]);
  __putc(' ');
  __puti(i);
  __putc(' ');

  s[2] = 'e';
  c = 2;
  s[c] -= 2;
  __putc(s[2]);
  __putc(' ');

  a[i] <<= 2;
  __puti(a[i] += 1);
  __putln();
  return 0;
}

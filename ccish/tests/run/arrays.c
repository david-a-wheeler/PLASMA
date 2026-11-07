/* Arrays: local arrays of int, char and pointers, constant and variable
 * indexes (int and char), multi-dimensional arrays, array parameters,
 * arrays used as pointers, &a[i], *a, i[a], and stores, compound
 * assignment and ++ / -- on elements. */
// listing-has: IDXW
void show(int n)
{
  __puti(n);
  __putc(' ');
}

int sum(int a[], int n)
{
  int s = 0, i;
  for (i = 0; i < n; i++)
    s += a[i];
  return s;
}

/* Sort n ints into increasing order (insertion sort). */
void sort(int a[], int n)
{
  int i, j, t;
  for (i = 1; i < n; i++) {
    t = a[i];
    j = i - 1;
    while (j >= 0 && a[j] > t) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = t;
  }
}

/* The trace of a 3 x 3 matrix, passed as a pointer to its rows. */
int trace(int m[][3])
{
  int i, t = 0;
  for (i = 0; i < 3; i++)
    t += m[i][i];
  return t;
}

/* m[2][j]: a constant row and a variable column through a pointer. */
int get(int m[][3], int j)
{
  return m[2][j];
}

/* Add 1 to m[1][j]. */
void bump(int m[][3], int j)
{
  m[1][j]++;
}

/* Copy the C string s to d. */
void copy(char *d, char *s)
{
  int i = 0;
  while ((d[i] = s[i]) != 0)
    i++;
}

int main()
{
  int a[5], i, *p;
  char s[8], c;
  int m[3][3];
  char *names[3];
  int t[6];

  /* constant indexes */
  a[0] = 10;
  a[1] = 20;
  a[2] = 30;
  a[3] = 40;
  a[4] = 50;
  show(a[0]);
  show(a[4]);
  show(a[1] + a[3]);
  __putln();

  /* variable indexes, compound assignment, ++ and -- */
  for (i = 0; i < 5; i++)
    a[i] += i;
  for (i = 0; i < 5; i++)
    show(a[i]);
  i = 2;
  a[i]++;
  ++a[i];
  a[i]--;
  show(a[i]++);
  show(a[i]);
  show(--a[i]);
  show(a[i] = 7);
  show(a[2]);
  __putln();

  /* char arrays and char indexes */
  for (c = 0; c < 7; c++)
    s[c] = 'a' + c;
  s[7] = 0;
  c = 3;
  __putc(s[c]);
  s[c] = 'D';
  for (i = 0; s[i]; i++)
    __putc(s[i]);
  __putc(' ');
  show(s[c] + 1);
  s[0] = 300;                 /* a char keeps the low byte */
  show(s[0]);
  __putln();

  /* arrays as pointers */
  p = a;
  show(*p);
  show(p[3]);
  p = a + 1;
  show(*p);
  show(p[-1]);
  show(*(a + 4));
  show(&a[3] - a);
  show(&a[4] - p);
  show(2[a]);
  show((*&a)[1]);
  *a = 11;
  show(a[0]);
  p[1] = 8;
  show(a[2]);
  i = 2;
  p[i]++;
  show(a[3]);
  show(a == &a[0]);
  show(p > a);
  __putln();

  /* functions taking arrays */
  show(sum(a, 5));
  t[0] = 5;
  t[1] = 3;
  t[2] = 9;
  t[3] = 1;
  t[4] = 7;
  t[5] = 3;
  sort(t, 6);
  for (i = 0; i < 6; i++)
    show(t[i]);
  __putln();

  /* two-dimensional arrays */
  for (i = 0; i < 3; i++)
    for (c = 0; c < 3; c++)
      m[i][c] = i * 3 + c;
  show(m[0][0]);
  show(m[1][2]);
  show(m[2][1]);
  show(trace(m));
  i = 1;
  m[i][2] = 50;
  m[2][i] = 60;
  show(m[1][2]);
  show(m[2][1]);
  m[i][i] += 100;
  show(m[1][1]);
  p = m[2];
  show(p[2]);
  show(*m[1]);
  show(**m);
  show(get(m, 1));
  bump(m, 0);
  show(m[1][0]);
  __putln();

  /* arrays of pointers */
  names[0] = "zero";
  names[1] = "one";
  names[2] = "two";
  for (i = 0; i < 3; i++)
    __putc(names[i][0]);
  __putc(' ');
  copy(s, names[2]);
  for (i = 0; s[i]; i++)
    __putc(s[i]);
  __putc(' ');
  show(names[1][2]);
  __putln();
  return 0;
}

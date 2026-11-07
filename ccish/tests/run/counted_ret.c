/* Counted loops whose body returns (approach.md, "Possible improvements
 * to the loop forms", 5): the return drops the loop's limit and count
 * from under its result first, which waits in temporaries. Results of
 * an int, a char, none, a 2-word and a 4-word struct, and a return from
 * a switch in the body; then 40 calls in a row, which would overflow
 * the eval stack if a return left the two values behind. */
// listing-count: 6 INCBRLE
struct pair {
  int a;
  int b;
};

struct quad {
  int a;
  int b;
  int c;
  int d;
};

int tab[5] = {4, 8, 15, 16, 23};
int done;

void show(int n)
{
  __puti(n);
  __putc(' ');
}

/* The index of the first element of tab at least n, or -1. */
int index_of(int n)
{
  int i;
  for (i = 0; i < 5; i++)
    if (tab[i] >= n)
      return i;
  return -1;
}

/* The nth letter: a char result. */
char letter(int n)
{
  int i;
  for (i = 0; i < 26; i++)
    if (i == n)
      return 'a' + i;
  return '?';
}

/* Count up in done, and stop at n: no result. */
void stop_at(int n)
{
  int i;
  for (i = 0; i < 10; i++) {
    done = i;
    if (i == n)
      return;
  }
}

/* The first element of tab over n, and its index: 2 words. */
struct pair pair_at(int n)
{
  struct pair p;
  int i;
  p.a = -1;
  p.b = -1;
  for (i = 0; i < 5; i++)
    if (tab[i] > n) {
      p.a = i;
      p.b = tab[i];
      return p;
    }
  return p;
}

/* n and its multiples, if n is from 1 to 4: 4 words. */
struct quad quad_at(int n)
{
  struct quad q;
  int i;
  q.a = 0;
  q.b = 0;
  q.c = 0;
  q.d = 0;
  for (i = 1; i <= 4; i++)
    if (i == n) {
      q.a = i;
      q.b = i * 2;
      q.c = i * 3;
      q.d = i * 4;
      return q;
    }
  return q;
}

/* A return from a switch in the body. */
int kind(int n)
{
  int i;
  for (i = 0; i < 3; i++)
    switch (n - i) {
    case 0:
      return 100 + i;
    }
  return 0;
}

int main()
{
  struct pair p;
  struct quad q;
  int j, s = 0;
  show(index_of(9));
  show(index_of(100));
  __putc(letter(3));
  __putc(' ');
  stop_at(4);
  show(done);
  stop_at(20);
  show(done);
  p = pair_at(10);
  show(p.a);
  show(p.b);
  p = pair_at(30);
  show(p.a);
  show(p.b);
  q = quad_at(3);
  show(q.a);
  show(q.b);
  show(q.c);
  show(q.d);
  q = quad_at(9);
  show(q.a);
  show(q.d);
  show(kind(1));
  show(kind(7));
  for (j = 0; j < 40; j++)
    s += index_of(j);
  show(s);
  __putln();
  return 0;
}

/* The other half of probe: see a.c. */
int f100198(int n);
extern int f100286;
int f100294(int n);
int f100000 = 4;

/* n less f100000. */
int f100199(int n) { return n - f100000; }

int main()
{
  puti(f100198(1)); putc(' ');
  puti(f100286); putc(' ');
  puti(f100294(3)); putc(' ');
  puti(f100199(10));
  putln();
  return 0;
}

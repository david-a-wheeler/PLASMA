/* static (M5 step 5). A static local lives in the data segment, like a
 * global, but has block scope. Its constant initializer is applied
 * once, when the module loads, so counter() counts from 10. It takes no
 * frame space: counter() and inits() have no frame (no ENTER), and
 * fill()'s 300-byte buffer, too big for the 255-byte frame, leaves it
 * just a parameter and a local (ENTER 4,1). Static locals in different
 * blocks are different objects. Their initializers work as a global's
 * do: strings, char arrays, and addresses of globals and of other static
 * locals. static on a global or a function changes nothing for now. */
// listing-count: 5 ENTER
// listing-has: ENTER 4,1
static int calls;
static int twice(int n);

void show(int n)
{
  __puti(n);
  __putc(' ');
}

int counter(void)
{
  static int n = 10;
  calls++;
  return n++;
}

static int twice(int n)
{
  return n * 2;
}

int fill(int c)
{
  static char line[300];
  int i;
  for (i = 0; i < 300; i++)
    line[i] = c;
  return line[299] + line[0];
}

int blocks(void)
{
  int r = 0;
  {
    static int x = 5;
    r = r + x;
    x++;
  }
  {
    static int x = 100;
    r = r + x;
    x++;
  }
  return r;
}

void inits(void)
{
  static char greeting[] = "hi";
  static char *msg = "yo";
  static int *self = &calls;
  static int arr[3] = {1, 2, 3};
  static int *pa = &arr[1];
  show(greeting[1]);
  show(msg[0]);
  show(*self);
  show(*pa);
  show(sizeof arr);
  show(sizeof greeting);
}

int main()
{
  int i;
  for (i = 0; i < 3; i++)
    show(counter());
  show(calls);
  show(twice(21));
  show(fill(65));
  show(blocks());
  show(blocks());
  inits();
  __putln();
  return 0;
}

// Large zero-filled globals, which cclink moves to the heap (approach.md,
// "Large zero-filled data on the heap"): zero at the start; constant
// and variable indexes; a char store at a constant index; a big
// struct's members; an array whose address is in an initializer, which
// stays in data; and a static one. The init function takes the block
// from heapalloc and zeroes it with memset.
// listing-has: __cmd_heapalloc (import)
// listing-has: __cmd_memset (import)
// listing-has: CS (18 chars)
// listing-count: 4 DAW
struct big {
  int a;
  char pad[300];
  int b;
};

int nums[200];
char text[256];
struct big rec;
static char hidden[400];
int kept[150];
int *pk = kept + 3;
char small[255];

void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int main()
{
  int i, s = 0;
  for (i = 0; i < 200; i++)
    s += nums[i];
  show(s);
  show(text[255]);
  show(rec.a + rec.b + hidden[399] + kept[149] + small[254]);
  __cmd_putln();
  nums[0] = 5;
  nums[199] = 7;
  for (i = 1; i < 199; i++)
    nums[i] = i;
  s = 0;
  for (i = 0; i < 200; i++)
    s += nums[i];
  show(s);
  text[255] = 'z';
  text[0] = 'a';
  i = 255;
  __cmd_putc(text[0]);
  __cmd_putc(text[i]);
  __cmd_putc(' ');
  rec.a = 100;
  rec.b = 23;
  rec.pad[299] = 9;
  show(rec.a + rec.b + rec.pad[299]);
  hidden[399] = 4;
  show(hidden[399]);
  *pk = 6;
  show(kept[3]);
  small[254] = 1;
  show(small[254]);
  __cmd_putln();
  return 0;
}

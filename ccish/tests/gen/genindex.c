/* genindex.c - a[i + c]: the constant goes into the array's address
 * (peep.c, rewrite_index), twice it for an int array, so the index is
 * one load, which then fuses with the ADD (LA a+c ADDLW i): 2 bytes and
 * 2 ops less. a is the first global, at data offset 0, so a[i - 1]'s
 * address is offset -1, which ccrel wraps and the index brings back.
 * Also an int array (IDXW), a local array (LLA n+c), a string, and an
 * index of more than one load. A pointer's p[i + 1] has no constant
 * address, and stays. */
char a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
int w[4] = {10, 20, 30, 40};

int prev(int i)   { return a[i - 1]; }
int next2(int i)  { return a[i + 2]; }
int wprev(int i)  { return w[i - 1]; }
int sum(int i, int j) { return a[i + j - 1]; }
int str(int i)    { return "abcdef"[i + 1]; }
int local(int i)
{
  char b[4];
  b[0] = 9; b[1] = 8; b[2] = 7; b[3] = 6;
  return b[i - 1];
}
int ptr(char *p, int i) { return p[i + 1]; }

int main(void)
{
  return prev(1) != 1 || next2(5) != 8 || wprev(4) != 40 || sum(3, 4) != 7
         || str(0) != 'b' || local(2) != 8 || ptr(a, 0) != 2;
}

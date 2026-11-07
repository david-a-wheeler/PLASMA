/* Global variables: int, char and pointer scalars, arrays of int, char
 * and pointers, a two-dimensional array, zero at load, global and local
 * index variables, pointers to globals, globals passed to functions and
 * changed by them, and locals and parameters that hide a global. */
// listing-has: LAW
// listing-has: SAW
// listing-has: LAB
// listing-has: SAB
int count;
char flag;
int *gp;
int table[5];
char big[1000];
int grid[3][4];
char *words[3];
int gi;

void show(int n)
{
  __puti(n);
  __putc(' ');
}

/* Count calls in a global. */
void tick(void)
{
  count++;
}

int sum(int a[], int n)
{
  int s = 0, i;
  for (i = 0; i < n; i++)
    s += a[i];
  return s;
}

int count2;                     /* declared between functions */

/* The parameter hides the global count. */
int hide(int count)
{
  return count * 2;
}

int main()
{
  int i, x;
  int loc[3];

  /* globals start at zero */
  show(count);
  show(flag);
  show(table[4]);
  show(big[999]);
  show(gp == 0);
  __putln();

  /* scalars */
  count = 5;
  count += 3;
  tick();
  tick();
  show(count);
  flag = 300;                 /* a char keeps the low byte */
  show(flag);
  flag++;
  show(flag++);
  show(flag);
  x = count2 = 7;
  show(x + count2);
  show(hide(4));
  show(count);
  {
    int count = 99;
    show(count);
  }
  show(count);
  __putln();

  /* arrays */
  for (i = 0; i < 5; i++)
    table[i] = i * i;
  show(table[3]);
  show(sum(table, 5));
  table[2] += 10;
  show(table[2]);
  gi = 4;
  show(table[gi]);
  table[gi]--;
  show(table[4]);
  loc[0] = 7;
  loc[1] = 8;
  loc[2] = 9;
  gi = 2;
  show(loc[gi]);
  loc[gi] = 1;
  show(loc[2]);
  big[999] = 'z';
  i = 998;
  big[i] = 'y';
  __putc(big[998]);
  __putc(big[i + 1]);
  __putc(' ');
  __putln();

  /* two dimensions */
  for (i = 0; i < 3; i++)
    for (gi = 0; gi < 4; gi++)
      grid[i][gi] = i * 10 + gi;
  show(grid[2][3]);
  show(grid[1][0]);
  gi = 1;
  show(grid[2][gi]);
  i = 1;
  show(grid[i][gi]);
  show(*grid[2]);
  show(**grid);
  __putln();

  /* pointers */
  gp = table;
  show(*gp);
  show(gp[1]);
  *gp = 50;
  show(table[0]);
  gp[gi] = 60;
  show(table[1]);
  gp = &count;
  *gp += 1;
  show(count);
  gp = &table[3];
  show(gp - table);
  show(*gp);
  words[0] = "cat";
  words[1] = "dog";
  words[2] = "emu";
  for (i = 0; i < 3; i++)
    __putc(words[i][1]);
  __putc(' ');
  show(&big[10] - big);
  __putln();
  return 0;
}

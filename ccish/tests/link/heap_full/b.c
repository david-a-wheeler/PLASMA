/* With a.c's and c.c's, large globals that the heap can't hold (plvm's
 * has about 64K): the init function prints a message and returns -1,
 * and main never runs. */
extern char one[];
char two[30000];

int main()
{
  one[0] = 1;
  two[0] = 2;
  __puts(pstr("ran"));
  return 0;
}

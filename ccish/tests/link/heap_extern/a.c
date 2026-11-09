// Defines big and buf, large zero-filled globals: big moves to the
// heap, and buf stays in data, since b.c's initializer holds its
// address.
int big[300];
char buf[512];
static int mine[200];

// Fill big, buf and mine.
void fill(void)
{
  int i;
  for (i = 0; i < 300; i++)
    big[i] = i;
  for (i = 0; i < 512; i++)
    buf[i] = i & 15;
  mine[199] = 8;
}

// Return mine's last element.
int last(void)
{
  return mine[199];
}

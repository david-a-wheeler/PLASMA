
/* The duplicate value is printed by num_text() (diag.c), which writes
 * numbers without the printf family: here the most negative int,
 * which can't be negated in 16 bits. */
int main()
{
  int x = 1;
  switch (x) {
  case -32767 - 1: x = 5;
  case -32768: x = 6;
  }
  return x;
}

// expect: a switch needs an integer value
/* C switches only on integers. */
int main()
{
  char *p = "hi";
  switch (p) {
  case 0: return 1;
  }
  return 0;
}


/* A switch has at most one default. */
int main()
{
  int x = 1;
  switch (x) {
  default: x = 2;
  case 1: x = 3;
  default: x = 4;
  }
  return x;
}

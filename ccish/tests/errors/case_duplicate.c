
// Each case value can appear once in a switch; the error is at the
// second one.
int main()
{
  int x = 1;
  switch (x) {
  case 1:
  case 2: x = 5;
  case 1 + 1: x = 6;
  }
  return x;
}

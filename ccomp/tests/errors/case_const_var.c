// expect: a const variable's value isn't a constant expression in C (a case label must be one)
/* A const variable isn't a constant expression in C, so it can't be a
 * case label (gcc says so too). */
const int TWO = 2;

int main()
{
  int x = 1;
  switch (x) {
  case TWO: x = 3;
  }
  return x;
}

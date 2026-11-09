// Function-like macros: arguments expanded before they are substituted
// (so MAX(MAX(a, b), c) works), #, ##, __VA_ARGS__, a macro name with
// no '(' after it, and ccish's own FRAME_TEMP and HELPER_FN (approach.md,
// "ccpp design", step 5).
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SQUARE(x) ((x) * (x))
#define STR(x) #x
#define XSTR(x) STR(x)
#define TEN 10
#define VAR(n) var_ ## n
#define SUM(...) sum3(__VA_ARGS__)
#define FRAME_TEMP(k) (-2 - (k))
#define HELPER_FN(k) (MAX_FUNCS + (k))
#define MAX_FUNCS 302
#define puti_twice(x) __puti(x); __puti(x)

int sum3(int a, int b, int c) { return a + b + c; }

int main()
{
  int VAR(one) = 1, VAR(two) = 2;
  int SQUARE = 5;               // no '(' after it: just a name
  __puti(MAX(MAX(3, 7), 5)); __putc(' ');
  __puti(SQUARE(var_one + var_two)); __putc(' ');
  __puts(pstr(STR(TEN))); __putc(' ');
  __puts(pstr(XSTR(TEN))); __putc(' ');
  __puti(SUM(1, 2, 3)); __putc(' ');
  __puti(SQUARE); __putc(' ');
  __puti(FRAME_TEMP(1)); __putc(' ');
  __puti(HELPER_FN(2)); __putc(' ');
  puti_twice(4);
  __putln();
  return 0;
}

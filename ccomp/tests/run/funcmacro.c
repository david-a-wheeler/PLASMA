/* Function-like macros: arguments expanded before they are substituted
 * (so MAX(MAX(a, b), c) works), #, ##, __VA_ARGS__, a macro name with
 * no '(' after it, and ccish's own FRAME_TEMP and HELPER_FN (approach.md,
 * "ccpp design", step 5). */
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
#define puti_twice(x) puti(x); puti(x)

int sum3(int a, int b, int c) { return a + b + c; }

int main()
{
  int VAR(one) = 1, VAR(two) = 2;
  int SQUARE = 5;               /* no '(' after it: just a name */
  puti(MAX(MAX(3, 7), 5)); putc(' ');
  puti(SQUARE(var_one + var_two)); putc(' ');
  puts(pstr(STR(TEN))); putc(' ');
  puts(pstr(XSTR(TEN))); putc(' ');
  puti(SUM(1, 2, 3)); putc(' ');
  puti(SQUARE); putc(' ');
  puti(FRAME_TEMP(1)); putc(' ');
  puti(HELPER_FN(2)); putc(' ');
  puti_twice(4);
  putln();
  return 0;
}

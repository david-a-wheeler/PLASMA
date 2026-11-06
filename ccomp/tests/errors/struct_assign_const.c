
/* A struct with a const member can't be assigned as a whole, even in
 * a struct inside it. */
struct id { const int n; };
struct rec { int a; struct id who; };

int main()
{
  struct rec r, s;
  r.a = 1;
  r = s;
  return 0;
}

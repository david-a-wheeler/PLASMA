
/* struct q first appears in f's prototype, so it is visible only there,
 * and the struct q defined later is a different type: gcc warns, and so
 * does ccish. The definition of f then conflicts with its prototype. */
void f(struct q *p);

struct q { int x; };

void f(struct q *p)
{
  p->x = 1;
}

int main()
{
  return 0;
}

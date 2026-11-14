// Struct assignment (M6 step 2): word moves between variables when that
// is no bigger than a call, memcpy otherwise and through pointers, a
// 3-byte struct (a word and a byte), *f() = s, a = *f(), chains, and a
// member of an assignment's result.
// listing-has: CALL $0000  ; __cmd_memcpy (import)
struct pt { int x, y; };
struct rgb { char r, g, b; };
struct big { int v[5]; };

struct pt gp, gq;
struct big b1, b2;
struct pt arr[3];
int calls;

// Return a pointer to arr[i], counting the calls.
struct pt *at(int i)
{
  calls++;
  return &arr[i];
}

// Print a point.
void show(struct pt *p)
{
  __cmd_puti(p->x); __cmd_putc(','); __cmd_puti(p->y); __cmd_putc(' ');
}

int main()
{
  struct pt a, b, c;
  struct rgb k, m;
  struct pt *p = &a;
  int i;

  a.x = 1; a.y = 2;
  b = a;                          // locals: word moves
  show(&b);
  gp = b;                         // local to global
  gq = gp;                        // globals: memcpy is smaller
  show(&gq);
  k.r = 10; k.g = 20; k.b = 30;
  m = k;                          // a word and a byte
  __cmd_puti(m.r + m.g + m.b); __cmd_putln();

  for (i = 0; i < 5; i++)
    b1.v[i] = i * i;
  b2 = b1;                        // 10 bytes: memcpy
  __cmd_puti(b2.v[4]); __cmd_putc(' ');
  arr[1] = *p;                    // through a pointer
  show(&arr[1]);
  *at(2) = a;                     // the address comes first
  show(&arr[2]);
  arr[2].y = 9;
  c = *at(2);                     // the source's address comes first
  show(&c);
  __cmd_puti(calls); __cmd_putln();

  a.x = 7;
  c = b = a;                      // a chain
  show(&b); show(&c);
  __cmd_puti((b = arr[2]).y); __cmd_putc(' ');    // a member of the result
  show(&b);
  i = 0;
  arr[i] = arr[i + 2];
  show(&arr[0]);
  *at(1) = *at(0);
  show(&arr[1]);
  __cmd_puti(calls); __cmd_putln();
  return 0;
}

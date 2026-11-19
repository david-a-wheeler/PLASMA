// Signed char where the host oracle can't see: in a struct (bytes at
// their offsets, no padding), volatile (every access happens, and the
// kept value is the promoted one), as a string literal's target type
// through a cast, and as a function pointer's target.
struct s {
  signed char a, b;   // offsets 0, 1
  int n;              // offset 2
  signed char c;      // offset 4
};                    // size 5

struct s g;
signed char w[4] = "hi";   // 104, 105, 0, 0

signed char f(signed char x) { return x * 2; }

int main(void)
{
  struct s s, *p;
  volatile signed char t;
  signed char (*fp)(signed char);
  char *str;

  // Members: each byte at its offset, no padding.
  s.a = (signed char)200; s.b = 100; s.n = 7; s.c = (signed char)128;
  __cmd_puti(s.a); __cmd_putc(' ');   // -56
  __cmd_puti(s.b); __cmd_putc(' ');   // 100
  __cmd_puti(s.n); __cmd_putc(' ');   // 7
  __cmd_puti(s.c);                    // -128
  __cmd_putln();
  __cmd_puti(sizeof(struct s));       // 5
  __cmd_putln();

  // Through a pointer; struct assignment copies the bytes.
  p = &s;
  p->a = (signed char)255;
  __cmd_puti(p->a); __cmd_putc(' ');  // -1
  g = s;
  __cmd_puti(g.a); __cmd_putc(' ');   // -1
  __cmd_puti(g.n);                    // 7
  __cmd_putln();

  // A string's bytes, in a signed char array: 0 to 127 read the same.
  __cmd_puti(w[0]); __cmd_putc(' ');  // 104
  __cmd_puti(w[1]); __cmd_putc(' ');  // 105
  __cmd_puti(w[2]); __cmd_putc(' ');  // 0
  __cmd_puti(w[3]);                   // 0
  __cmd_putln();

  // A volatile signed char: every access happens, and a kept value is
  // the object's (promoted) value.
  t = (signed char)200;
  __cmd_puti(t); __cmd_putc(' ');     // -56
  t = 100;
  __cmd_puti(t); __cmd_putc(' ');     // 100
  __cmd_puti(t = (signed char)250); __cmd_putc(' ');  // -6 (kept)
  __cmd_puti(t);                      // -6
  __cmd_putln();

  // A function pointer to a signed char function.
  fp = f;
  __cmd_puti(fp((signed char)200));   // -112
  __cmd_putln();

  // char * and signed char * are not the same type: a cast connects
  // them, and the bytes are the same.
  str = "hi";
  __cmd_puti(((signed char *)str)[0]);  // 104
  __cmd_putln();

  return 0;
}

// The code for struct members (approach.md, "M6 design"). A member of
// a variable is a variable at a fixed offset: l.b is LLW 1 and g.c is
// LAW data+3. One reached through a pointer adds its offset (p->c:
// LLW 0 CN 3 ADD LW), except at offset 0 (p->a: LLW 0 LB). Through a
// constant pointer, such as a block of I/O registers, the offset is
// added at compile time: one LAW $0303.
// listing-has: SLW 1
// listing-has: LLW 1
// listing-has: data+3
// listing-has: LAW $0303
// listing-lacks: CW $0300
struct s { char a; int b; int c; };

struct s g;

int get(struct s *p)
{
  return p->a + p->c;
}

int main()
{
  struct s l;
  int v;
  l.b = 5;
  g.c = 6;
  __cmd_puti(l.b + g.c + get(&g));
  __cmd_putln();
  v = ((struct s *)0x300)->c;
  return v - v;
}

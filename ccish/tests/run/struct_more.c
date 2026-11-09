// More struct forms (M6 step 1): a declared-then-defined tag, a struct
// defined inside another's member list (its tag is still at file
// scope), an anonymous struct, a tag defined inside a function, members
// reached through calls and indexed pointers, array members, char
// members, and ++ and op= on members.
struct later;                       // declared, completed below
struct later *lp;

struct later { int a; char c; };

struct outer {
  struct inner { int x; char tag; } in;   // offset 0, size 3
  int z;                                  // offset 3
  int arr[3];                             // offset 5
};

struct inner solo;                  // the nested tag is visible here
struct outer o;
struct { int u, v; } anon;          // a struct type with no tag

// Return a pointer to o, counting the calls.
int calls;
struct outer *get(void)
{
  calls++;
  return &o;
}

// A tag defined in a function is visible only there.
int local_tag(void)
{
  struct later { char only; } x;
  x.only = 5;
  return sizeof x + x.only;
}

int main()
{
  struct later l;
  struct outer *p = &o;
  const struct outer *cp = &o;
  int i = 1;

  lp = &l;
  lp->a = 1000;
  lp->c = 300;                    // a char keeps the low byte: 44
  __puti(l.a); __putc(' ');
  __puti(l.c); __putc(' ');
  __puti(sizeof(struct later)); __putc(' ');
  __puti(local_tag()); __putc(' ');
  __puti(sizeof(struct later)); __putln();

  solo.x = 2;
  o.in.x = 3;
  o.in.tag = 'T';
  o.z = 4;
  __puti(sizeof solo); __putc(' ');
  __puti(sizeof(struct outer)); __putc(' ');
  __puti((char *)&o.z - (char *)&o); __putc(' ');
  __puti(solo.x + o.in.x + o.z); __putc(' ');
  __putc(cp->in.tag); __putln();

  anon.u = 6;
  anon.v = anon.u * 7;
  __puti(anon.v); __putc(' ');
  __puti(sizeof anon); __putln();

  // through a call: its address is on the stack
  get()->z = 10;
  __puti((*get()).z); __putc(' ');
  get()->arr[i] = 20;
  __puti(get()->arr[1] + get()->in.x); __putc(' ');
  __puti(calls); __putln();

  // array members, through a variable and a pointer
  o.arr[0] = 1;
  o.arr[2] = 3;
  p->arr[i] += 5;
  for (i = 0; i < 3; i++) {
    __puti(p->arr[i]); __putc(' ');
  }
  __puti(p[0].arr[2]); __putln();

  // ++, -- and op= on members
  p->z++;
  ++p->z;
  o.in.x--;
  p->in.x *= 10;
  __puti(o.z); __putc(' ');
  __puti(o.in.x); __putc(' ');
  __puti(p->z++ + 1); __putc(' ');
  __puti(o.z); __putc(' ');
  __puti(*&p->z); __putc(' ');
  __puti(&p->arr[2] - &o.arr[0]); __putln();
  for (struct later *q = &l; q; q = 0)
    __puti(q->a);
  __putln();
  return 0;
}

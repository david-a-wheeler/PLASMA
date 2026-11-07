/* genunion.c: a union's machine code. A union is stored as a struct
 * whose tag is marked a union's (src/type.c): every member starts at
 * byte 0, and the union is the size of its largest member. The
 * listing pins both: a union's members are all addressed at the same
 * offset (u.a and u.b at byte 0, s.c and s.d at byte 2; mi and mc
 * differ only in LW against LB at the same offset), and the frame
 * pins the max: u takes the int's two bytes, not the three of the
 * sum, so z sits at the offset a wrong size would move it from. */
union W { int a; char b; };
union S { char c; char d; };

union W gw;

struct Box { char t; union W u; int n; };
struct Box box;

int mi(union W *p) { return p->a; }
int mc(union W *p) { return p->b; }

int sm(union S s) { return s.c + s.d; }

int main(void)
{
  union W u;
  union S s;
  int z;
  u.a = 1;      /* at u's byte 0 */
  u.b = 2;      /* at u's byte 0, the same place */
  z = u.a;      /* and back through the int */
  s.c = 3;
  s.d = 4;
  /* 2 + 2 + 2 + (4 + 4) + 2: u.b's 2 overwrote u.a's low byte, s.d's 4
   * overwrote s.c's 3 (gcc gives 16 too) */
  return mi(&u) + mc(&u) + z + sm(s) + z == 16 ? 0 : 1;
}

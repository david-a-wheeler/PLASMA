// Do-nothing counted loops with constant ends go (irloop.c,
// counted_loop): only the variable's last value is left, computed in
// 16 bits. Steps of 1 and 3 up, 1 and 4 down, a step that doesn't land
// on the limit, a distance of 60000 (more than an int holds), a limit
// next to 32767, and a loop that never runs (left as written). No
// counted loop's op is left, but never()'s test (one ISLT); gcc agrees.
// A loop that must run has a volatile in it (genplain.c).
// listing-count: 0 INCBRLE
// listing-count: 0 DECBRGE
// listing-count: 0 ADDBRLE
// listing-count: 0 SUBBRGE
// listing-count: 0 BRLT
// listing-count: 0 BRGT
int up(void) { int i; for (i = 0; i < 500; i++) ; return i; }
int le(void) { int i; for (i = 3; i <= 10; i += 3) ; return i; }
int lt3(void) { int i; for (i = 3; i < 10; i += 3) ; return i; }
int dn(void) { int i; for (i = 10; i > 0; i--) ; return i; }
int ge(void) { int i; for (i = 10; i >= -5; i -= 4) ; return i; }
int big(void) { int i; for (i = -30000; i < 30000; i++) ; return i; }
int top(void) { int i; for (i = 32760; i <= 32766; i++) ; return i; }
int never(void) { int i; for (i = 9; i < 5; i++) ; return i; }
int main(void)
{
  __cmd_puti(up()); __cmd_putc(' '); __cmd_puti(le()); __cmd_putc(' '); __cmd_puti(lt3()); __cmd_putc(' ');
  __cmd_puti(dn()); __cmd_putc(' '); __cmd_puti(ge()); __cmd_putc(' '); __cmd_puti(big()); __cmd_putc(' ');
  __cmd_puti(top()); __cmd_putc(' '); __cmd_puti(never()); __cmd_putln();
  return 0;
}

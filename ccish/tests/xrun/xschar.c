// xschar.c - signed char: a byte object whose value, in an expression,
// is its byte sign-extended to int (C's integer promotion), while its
// stored byte is the low byte of whatever was assigned. The cross-check
// runs this against the host compiler (where signed char is signed too,
// whatever char's signedness is). Storing a wider value keeps the low
// byte, and reading it back gives the signed value of that byte.
//
// Storing a constant byte that does not fit a signed char (-128 to
// 127) is written as an explicit cast: the host oracle is checked with
// -Werror, and clang, unlike gcc, rejects the implicit conversion of
// such a constant. The body is in section functions (a function's code
// is bounded, approach.md).
#include "ptest.h"

// Function forms: a signed char parameter and return, and the promotion
// of a signed char to an int return (the low byte sign-extended).
int add2(signed char a, signed char b) { return a + b; }
signed char byte(int x) { return x; }
int sign_of(signed char c) { return c; }
signed char neg(signed char c) { return -c; }

// A signed char through an address, and a pointer's arithmetic with a
// signed char index.
int via_ptr(signed char *p, int i) { return p[i]; }

signed char g = (signed char)200;   // global: -56
const signed char cg = -1;          // const: -1

// Storing a wider value keeps the low byte; reading gives the signed
// value of that byte.
void t_store(void)
{
  signed char c;
  c = (signed char)200; puti(c); putln();   // -56
  c = (signed char)255; puti(c); putln();   // -1
  c = (signed char)32768; puti(c); putln(); // 0
  c = (signed char)128; puti(c); putln();   // -128
  c = 100; puti(c); putln();                // 100
  c = -128; puti(c); putln();               // -128
  c = 127; puti(c); putln();                // 127
}

// Arithmetic, comparison, compound assignment and ++ / --, all on the
// promoted (sign-extended) int value.
void t_arith(void)
{
  signed char c;
  int x;
  c = (signed char)200;
  puti(c + 100); putln();          // -56 + 100 = 44
  puti(c * 2); putln();            // -112
  c = -2; puti(c * 3); putln();    // -6
  puti(-c); putln();               // 2
  puti(~c); putln();               // ~-2 = 1
  c = 127; puti(c + 1); putln();   // 128 (int, not wrapped)
  puti(c - 200); putln();          // 127 - 200 = -73
  c = -1; puti(c < 0); putln();    // 1
  puti(c >= 0); putln();           // 0
  puti(c == -1); putln();          // 1
  x = c; puti(x == 255); putln();  // -1 is not 255
  c = (signed char)200;
  puti(c < 50); putln();           // -56 < 50, 1
  c = -1; c += 2; puti(c); putln();          // 1
  c = (signed char)250; c *= 2; puti(c); putln();  // -12
  c = 100; puti(c++); putln();               // 100 (old)
  puti(c); putln();                          // 101
  c = -100; puti(c--); putln();              // -100 (old)
  puti(c); putln();                          // -101
  c = 0; c++; puti(c); putln();              // 1
  c = 0; c--; puti(c); putln();              // -1
}

// The ?: and a switch, on the promoted value.
void t_branch(void)
{
  signed char c;
  c = -1;
  puti(c ? -10 : 10); putln();          // -10
  puti(0 ? 10 : -10); putln();          // -10
  c = -1;
  switch (c) {
    case -1: puti(1000); break;
    case 0: puti(2000); break;
    default: puti(3000); break;
  }
  putln();
}

// Casts: to int sign-extends; from a wide int keeps the low byte. A
// char (unsigned) and a signed char hold the same byte, different int.
void t_cast(void)
{
  signed char c, sc;
  char u;
  int x;
  c = (signed char)200; puti((int)c); putln();   // -56
  puti((signed char)-200); putln();              // 56
  puti((signed char)1000); putln();              // -24
  puti((signed char)128); putln();               // -128
  // (unsigned)c is 0xFFFF (16 bits); puti's int parameter wraps it:
  // -1, as the host's wider unsigned wrapping to int does too.
  c = -1; puti((unsigned)c); putln();            // -1
  u = 200; sc = (signed char)200;
  puti(u); putln();             // 200
  puti(sc); putln();            // -56
  x = sc; puti(u == x); putln();// 200 == -56, 0
}

// Functions: parameter, return, and promotion to an int return.
void t_func(void)
{
  signed char c;
  puti(add2(-1, (signed char)200)); putln();     // -57
  puti(add2(-128, 127)); putln();                // -1
  puti(byte(200)); putln();                      // -56
  puti(byte(300)); putln();                      // 44
  c = 100; puti(sign_of(c)); putln();            // 100
  c = (signed char)200;
  puti(sign_of(c)); putln();                     // -56
  c = 127; puti(neg(c)); putln();                // -127
}

// Arrays and pointers: a signed char index is promoted before it is
// scaled.
void t_arr(void)
{
  signed char d[6], c, *p;
  int i, s;
  d[0] = -1; d[1] = 127; d[2] = (signed char)128; d[3] = (signed char)255;
  d[4] = (signed char)32768; d[5] = (signed char)200;
  puti(d[2]); putln();     // -128
  puti(d[3]); putln();     // -1
  puti(d[4]); putln();     // 0
  puti(d[5]); putln();     // -56
  s = 0;
  for (i = 0; i < 6; i++)
    s += d[i];
  puti(s); putln();        // -59
  p = d;
  puti(via_ptr(p, 1)); putln();   // 127
  c = -1;
  puti(*(p + (1 - c))); putln();  // *(p + 2) = -128
}

// A global and a const signed char.
void t_glob(void)
{
  puti(g); putln();            // -56
  g = 100; puti(g); putln();   // 100
  puti(cg); putln();           // -1
  puti(cg * 100); putln();     // -100
}

// Assignment keeps the value on the stack as the object's (promoted)
// value, not the raw byte; sizeof is 1; a signed-char loop is right;
// an address and a dereference.
void t_rest(void)
{
  signed char c, *p;
  int s, x;
  c = (signed char)200;
  x = (c = (signed char)300); puti(x); putln();  // 44
  c = (signed char)200;
  x = (c = (signed char)250); puti(x); putln();  // -6
  puti(sizeof(signed char)); putln();            // 1
  s = 0;
  for (c = -3; c < 5; c++)
    s += c;
  puti(s); putln();             // 4
  c = (signed char)200;
  p = &c; puti(*p); putln();    // -56
  *p = 100; puti(c); putln();   // 100
}

int main(void)
{
  t_store();
  t_arith();
  t_branch();
  t_cast();
  t_func();
  t_arr();
  t_glob();
  t_rest();
  return 0;
}

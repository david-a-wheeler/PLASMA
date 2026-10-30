// expect: error 30: too many local variables in 'big'
/* The frame holds at most 255 bytes: a 250-byte array fits, and the
 * third int after it doesn't. */
int big() {
  char pad[250];
  int a;
  int b;
  int c;
  pad[0] = 1;
  return a + b + c;
}
int main() { return big(); }

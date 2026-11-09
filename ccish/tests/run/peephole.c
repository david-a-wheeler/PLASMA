// Peephole patterns: an assignment whose value is unused is a plain
// store (SLW, not DLW + DROP); -5 is one constant (CFFB 251, no NEG);
// storing x then loading x again is one DLW; x + 1 is INCR. main is
// void, so even init has no DROP.
// listing-count: 1 DLW 0
// listing-lacks: DROP
// listing-lacks: NEG
// listing-has: CFFB 251
// listing-has: INCR
void main()
{
  int x, y;
  x = -5;
  y = x + 1;
  __puti(x); __putc(' '); __puti(y); __putln();
}

/* More left operands held back for folding than MAX_HELD (8): each
 * past it is emitted instead, and the sum is still right. 1 + ... + 10
 * is 55, plus x (1). */
int main()
{
  int x = 1;
  puti(1 + (2 + (3 + (4 + (5 + (6 + (7 + (8 + (9 + (10 + x))))))))));
  putln();
  return 0;
}

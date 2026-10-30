// expect: enum value too large for an int (over 32767)
/* An enum constant must fit in an int, which is 16 bits. */
enum { BIG = 32767, BIGGER };

int main()
{
  return 0;
}

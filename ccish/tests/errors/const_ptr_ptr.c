
/* Only the first level may gain const: char ** to const char ** is not
 * allowed in C (it would let a const char be changed), and GCC 14
 * makes it an error too. */
int main()
{
  char *s = "x";
  char **pp = &s;
  const char **cpp = pp;
  return cpp == 0;
}

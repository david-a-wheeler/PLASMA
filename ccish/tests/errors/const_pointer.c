
// A const pointer (char *const) can't be changed, though what it points
// to can.
char buf[4];

int main()
{
  char *const p = buf;
  *p = 'a';
  p = buf + 1;
  return 0;
}

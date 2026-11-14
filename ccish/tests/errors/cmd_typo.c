// A __cmd_ name that is none of the CMDSYS functions (a typo for
// __cmd_strcpy) is "not a CMDSYS function", naming it, not a bare
// undefined reference (doc/linking.md, 3.2).
char *__cmd_strcp(char *d, const char *s);

char buf[8];

int main(void)
{
  __cmd_strcp(buf, "x");
  return 0;
}

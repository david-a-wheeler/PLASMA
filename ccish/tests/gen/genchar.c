// genchar.c - the char local and parameter: the byte forms of the
// loads and stores (LLB, SLB), a small & constant becomes the byte
// op ANDI, and a char that is stored, kept and never read back comes
// out of the dead store as the byte it keeps (ANDI 255).
int letter(void)
{
  char c;
  c = 'A';
  return c;
}

char ch(void)
{
  char c;
  c = 'A';
  return c;
}

int mask(char c)
{
  return c & 0x3F;
}

int main(void)
{
  letter();
  ch();
  mask('a');
  return 0;
}

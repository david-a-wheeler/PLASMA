// The milestone 1 example from approach.md.
int add(int a, int b) { return a + b; }

int main()
{
  __cmd_puts(pstr("Hello"));
  __cmd_puti(add(2, 3));
  __cmd_putln();
  return 0;
}

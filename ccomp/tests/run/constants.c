/* Constants that need each encoding: CN, CB, CFFB, CW and MINUS1.
 * puti() prints signed values, so 32768 and up print as negative. */
void show(int n)
{
  puti(n);
  putc(' ');
}

int main()
{
  show(0); show(15); show(16); show(255); show(256);
  show(32767); show(32768); show(65535); show(0xFF00); show(0xFFFE);
  show(0xFEFF);
  putln();
  show(0x7f); show(010); show(0); show('A'); show('\n'); show('\x41');
  show('\101'); show('\\');
  putln();
  return 0;
}

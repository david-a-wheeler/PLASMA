// Constant folding in the parser, with C's 16-bit int rules.
// Every operation on constants alone is computed at compile time, so
// only the operations that involve x appear in the code. (The two
// 2 * x become DUP ADD in the peephole stage.) The one NEG and the one
// AND (an ANDI) are the init function's, for main's exit status.
// listing-lacks: MUL
// listing-count: 2 DUP
// listing-count: 2 DIV
// listing-count: 1 SHL
// listing-lacks: MOD
// listing-lacks: SHR
// listing-count: 1 NEG
// listing-lacks: COMP
// listing-lacks: NOT
// listing-count: 1 AND
// listing-lacks: IOR
// listing-lacks: XOR
// listing-has: INCR
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int main()
{
  int x;
  // basic operators and precedence
  show(2 * 3); show(-(1 << 4)); show('a' + 1); show(1 + 2 * 3 - 4);
  show((2 + 3) * (4 - 1)); show(1 - (2 * 3));
  __cmd_putln();
  // division truncates toward zero; % has the dividend's sign
  show(7 / 2); show(-7 / 2); show(7 / -2); show(-7 / -2);
  show(7 % 3); show(-7 % 3); show(7 % -3); show(-7 % -3);
  __cmd_putln();
  // 16-bit wraparound
  show(32767 + 1); show(300 * 300); show(65535 + 1); show(0xFFFF);
  show(1 << 15); show(-32768 - 1);
  __cmd_putln();
  // arithmetic shift right, bitwise and logical operators
  show(-1 >> 1); show(-16 >> 2); show(0x7FFF >> 14);
  show(~0); show(!0); show(!5); show(!!7);
  show(0xF0 & 0x3C); show(0xF0 | 0x0F); show(0xFF ^ 0x0F);
  __cmd_putln();
  // constants mixed with a variable
  x = 10;
  show(1 - x); show(100 / x); show(2 * x + 3 * 4); show(x + 2 * 3);
  show(1 << x); show(20 - x - 3); show(1 - (2 * x)); show(1 + x);
  __cmd_putln();
  x += 2 * 3; show(x);
  x /= 4; show(x);
  __cmd_putln();
  return 0;
}

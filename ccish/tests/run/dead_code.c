// Dead-code removal: nothing after an unconditional return is emitted,
// including the implicit "return 0" at the end of a function.
// one, two and main have one return each (RET, since they have no
// frame), plus the init function's; putc('X') (CB 88) is gone.
// listing-count: 4 RET
// listing-lacks: LEAVE
// listing-lacks: CB 88
int one() { return 1; }

void two()
{
  __cmd_putc('2');
  return;
  __cmd_putc('X');
}

int main()
{
  __cmd_puti(one());
  two();
  __cmd_putln();
  return 0;
}

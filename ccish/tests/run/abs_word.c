// abs_word.c: a word store through a constant address (peep.c): the
// constant and the store are one absolute op, SAW, as the load is one
// (LAW) and the byte ones are (LAB, SAB). Such addresses are plain
// memory, but not zero, so no value is assumed before a store: each
// store is checked by reading it back, and the two addresses' values
// are checked after both stores, so a store to the wrong address
// shows.
void setw(void)
{
  *(int *)0x0800 = 7;
}

void setw2(void)
{
  *(int *)0x0802 = 9;
}

int getw(void)
{
  return *(int *)0x0800;
}

int getw2(void)
{
  return *(int *)0x0802;
}

int main(void)
{
  setw();
  __cmd_puti(getw());
  __cmd_putc(' ');
  setw2();
  __cmd_puti(getw());
  __cmd_putc(' ');
  __cmd_puti(getw2());
  __cmd_putln();
  return 0;
}

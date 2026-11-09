// genswitch.c - the switch: a SEL to a case table of values and
// labels, with the default the fall-through after the table.
int pick3(int x)
{
  switch (x) {
  case 1:
    return 10;
  case 2:
    return 20;
  default:
    return 0;
  }
}

int main(void)
{
  return pick3(2) == 20;
}

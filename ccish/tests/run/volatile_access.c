// A volatile object is read and written just as the program says
// (approach.md issue 25), beside a twin without volatile. The globals,
// in order: vx data+0, px data+2, vy data+4, py data+6.
//   vx; vx;  two loads, kept (a plain x; loads nothing);
//   vy++     one load and one store, and main's __cmd_puti(vy) one more.
// listing-count: 2 ; data+0
// listing-lacks: ; data+2
// listing-count: 3 ; data+4
volatile int vx = 7;
int px = 8;
volatile int vy = 1;
int py = 1;

void reads(void)
{
  vx;
  vx;
  px;
  px;
}

void incs(void)
{
  vy++;
  py++;
}

int main()
{
  reads();
  incs();
  __cmd_puti(vy);
  __cmd_putc(' ');
  __cmd_puti(py);
  __cmd_putln();
  return 0;
}

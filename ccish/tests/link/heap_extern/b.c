// Uses a.c's large globals through extern declarations, one's address
// in an initializer; its own large global moves too.
extern int big[];
extern char buf[512];
char *pb = buf + 17;
int own[128];
void fill(void);
int last(void);

int main()
{
  __cmd_puti(big[0] + big[299] + *pb + own[127]);
  __cmd_putc(32);
  fill();
  own[127] = 1;
  __cmd_puti(big[299] + *pb + own[127] + last());
  __cmd_putln();
  return 0;
}

/* Uses a.c's large globals through extern declarations, one's address
 * in an initializer; its own large global moves too. */
extern int big[];
extern char buf[512];
char *pb = buf + 17;
int own[128];
void fill(void);
int last(void);

int main()
{
  puti(big[0] + big[299] + *pb + own[127]);
  putc(32);
  fill();
  own[127] = 1;
  puti(big[299] + *pb + own[127] + last());
  putln();
  return 0;
}

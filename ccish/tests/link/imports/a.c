// PLASMA imports across objects (approach.md issue 22, decision 35):
// both objects import CMDSYS's data by the same asm label, and b.c has
// its address in data; a.c imports FILEIO's table after a pragma. A
// module only a pragma names, whose imports are never used, isn't a
// dependency, and a pragma'd prototype that another object defines
// (b_version) links to that definition, not an import.
// listing-has: ; import cmdsys as CMDSYS
// listing-has: ; import fileio as fileio
// listing-has: ; dependencies: CMDSYS FILEIO
// listing-lacks: UNUSED
// listing-lacks: unused_fn
// listing-lacks: b_version as
#pragma plasma import UNUSED
extern int unused_fn(int);          // never used
#pragma plasma import FILEIO
struct t_fileio
{
  int (*getpfx)(char *);
  int (*setpfx)(char *);
  int (*getfileinfo)(char *, char *);
  int (*setfileinfo)(char *, char *);
  int (*geteof)(int);
  int (*seteof)(int);
  int (*iobufalloc)(int);
  int (*open)(char *);
};
extern struct t_fileio fileio;
int b_version(void);                // b.c defines it
struct t_cmdsys { int sysver; };
extern struct t_cmdsys cmdsys __asm__("CMDSYS");

int main()
{
  __puti(b_version() >> 8); __putc(' ');
  __puti(fileio.open(pstr("nosuchfile"))); __putc(' ');
  __puti(cmdsys.sysver & 255);
  __putln();
  return 0;
}

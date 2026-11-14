// Imports from other PLASMA modules (approach.md issue 22, decision
// 35): CMDSYS's own data, named by an asm label (CMDSYS is always
// loaded, so no pragma), and FILEIO's table of functions, after a
// pragma that makes FILEIO a dependency. Under plvm, FILEIO is built
// in, so the dependency loads no file. The test opens its own module
// file and reads its header's magic, $6502; then a file that doesn't
// exist, which sets CMDSYS's error byte.
// img-skip: it reads its own module file, a REL module's header.
extern void __cmd_puti(int);              // still CMDSYS's, after the pragma

struct t_cmdsys
{
  int sysver;
  char *syspath;
  char *cmdline;
  int (*modexec)(char *);
  int (*sysopen)(char *);
  int (*sysclose)(int);
  int (*sysread)(int, char *, int);
  int (*syswrite)(int, char *, int);
  char syserr;
};
extern struct t_cmdsys cmdsys __asm__("CMDSYS");

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
  int (*close)(int);
  int (*read)(int, char *, int);
  int (*write)(int, char *, int);
};
extern struct t_fileio fileio;
extern void __cmd_putln(void);

int main()
{
  char buf[4];
  int ref;
  __cmd_puti(cmdsys.sysver >> 8); __cmd_putc(' ');      // 2
  __cmd_puti(cmdsys.sysver & 255); __cmd_putc(' ');     // $20, 32
  __cmd_puti(cmdsys.syspath != 0); __cmd_putln();
  ref = fileio.open(pstr("imports"));
  __cmd_puti(ref != 0); __cmd_putc(' ');
  __cmd_puti(fileio.read(ref, buf, 4)); __cmd_putc(' ');
  __cmd_puti(buf[2]); __cmd_putc(' ');                  // $02
  __cmd_puti(buf[3]); __cmd_putc(' ');                  // $65
  __cmd_puti(fileio.close(ref)); __cmd_putln();
  cmdsys.syserr = 0;
  ref = fileio.open(pstr("nosuchfile"));
  __cmd_puti(ref); __cmd_putc(' ');
  __cmd_puti(cmdsys.syserr != 0); __cmd_putln();
  return 0;
}

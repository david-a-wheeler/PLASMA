/* Imports from other PLASMA modules (approach.md issue 22, decision
 * 35): CMDSYS's own data, named by an asm label (CMDSYS is always
 * loaded, so no pragma), and FILEIO's table of functions, after a
 * pragma that makes FILEIO a dependency. Under plvm, FILEIO is built
 * in, so the dependency loads no file. The test opens its own module
 * file and reads its header's magic, $6502; then a file that doesn't
 * exist, which sets CMDSYS's error byte. */
extern void __puti(int);              /* still CMDSYS's, after the pragma */

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
extern void __putln(void);

int main()
{
  char buf[4];
  int ref;
  __puti(cmdsys.sysver >> 8); __putc(' ');      /* 2 */
  __puti(cmdsys.sysver & 255); __putc(' ');     /* $20, 32 */
  __puti(cmdsys.syspath != 0); __putln();
  ref = fileio.open(pstr("imports"));
  __puti(ref != 0); __putc(' ');
  __puti(fileio.read(ref, buf, 4)); __putc(' ');
  __puti(buf[2]); __putc(' ');                  /* $02 */
  __puti(buf[3]); __putc(' ');                  /* $65 */
  __puti(fileio.close(ref)); __putln();
  cmdsys.syserr = 0;
  ref = fileio.open(pstr("nosuchfile"));
  __puti(ref); __putc(' ');
  __puti(cmdsys.syserr != 0); __putln();
  return 0;
}

/* xload.c: XLOAD's first part (../doc/xload.md, section 4), an
 * ordinary REL module: +XLOAD [-V] PROGRAM ARGS loads PROGRAM, an
 * XLOAD image (ccrel -x), and runs it with the command line PROGRAM
 * ARGS. This part is all that PLASMA's loader runs. It finds which VM
 * it's on, captures the VM's state for the hand-off, reads the
 * program's header, and copies the second part (xload2.c, an image
 * held here as data, xl2img.h) to where the program won't load over
 * it, relocates it, and calls it. The second part loads the program
 * at the bottom of the heap, where this module is, and hands off to
 * it. */

/* Two small functions first, so that they're this module's first two
 * functions and the loader gives them its first two def entries, one
 * after the other: their spacing and vector tell the VM (4.2), and the
 * first is the module's bottom, where the program goes. */
static int xl_probe0(void)
{
  return 0;
}

/* The second of the two (xl_probe0()). */
static int xl_probe1(void)
{
  return 1;
}

#include "xlcore.c"
#include "xl2img.h"         /* xl2_image[], the second part */

/* The capture routine (4.5): 6502 code that returns the 6502 stack
 * pointer (high byte) and the VM's evaluation stack index X (low). */
static unsigned char xl_capture[] = {
  0x86, 0xE7,                 /* STX TMPL */
  0xBA,                       /* TSX */
  0x8A,                       /* TXA */
  0xA6, 0xE7,                 /* LDX TMPL */
  0xCA,                       /* DEX */
  0x95, 0xA0,                 /* STA ESTKH,X */
  0xA5, 0xE7,                 /* LDA TMPL */
  0x95, 0xC0,                 /* STA ESTKL,X */
  0x60                        /* RTS */
};

/* The VM's state when main began (4.5): captured SP and X, and IFP. */
static int xl_spx, xl_ifp;

/* Which VM XLOAD is on (4.2), from its own first two def entries: XL_*,
 * or 0 if it's one XLOAD doesn't know. */
static int xl_findvm(void)
{
  unsigned char *e0 = (unsigned char *)xl_probe0;
  unsigned char *e1 = (unsigned char *)xl_probe1;
  int vec = e0[1] | (e0[2] << 8);
  if (e1 - e0 == 5 && vec == XL_VEC_APPLE)
    return XL_APPLE64;
  if (e1 - e0 == 5 && vec == XL_VEC_PLVMX)
    return XL_PLVM;
  return 0;
}

/* Take XLOAD's own word, and -V if it's next, off the front of the
 * command line, leaving PROGRAM ARGS; return 1 for -V. */
static int xl_args_off(void)
{
  char *cl = xl_sys.cmdline;
  int n = *cl, i = 1, r = 1, v = 0;
  while (i <= n && cl[i] != ' ')
    i++;
  while (i <= n && cl[i] == ' ')
    i++;
  if (i + 1 <= n && cl[i] == '-' && (cl[i + 1] == 'V' || cl[i + 1] == 'v')
      && (i + 2 > n || cl[i + 2] == ' ')) {
    v = 1;
    i += 2;
    while (i <= n && cl[i] == ' ')
      i++;
  }
  while (i <= n)
    cl[r++] = cl[i++];
  *cl = (char)(r - 1);
  return v;
}

/* Load the program and run it; the first part's work after main's
 * capture. Returns main's exit status for XLOAD's own (0 to 255). */
static int xl_run(void)
{
  struct xl_args a;
  char path[17];
  int i, n, ref, (*entry)(struct xl_args *);
  unsigned char *end, *t, *zero, *zend, *top;
  unsigned need;
  char *cl;

  a.vm = xl_findvm();
  a.esize = 5;
  a.vec = a.vm == XL_APPLE64 ? XL_VEC_APPLE : XL_VEC_PLVM;
  a.base = (unsigned char *)xl_probe0;
  a.spx = xl_spx;
  a.ifp = xl_ifp;
  a.init = 0;
  a.verbose = xl_args_off();
  if (!a.vm) {
    xl_say();
    __puts(pstr("doesn't know this VM yet"));
    __putln();
    return 1;
  }
  cl = xl_sys.cmdline;
  n = *cl;
  for (i = 1; i <= n && cl[i] != ' ' && i <= 16; i++)
    path[i] = cl[i];
  path[0] = (char)(i - 1);
  if (path[0] == 0) {
    __puts(pstr("usage: +XLOAD [-V] PROGRAM [ARGS]"));
    __putln();
    return 2;
  }
  a.name = path;

  /* The program's sizes, from its header. */
  ref = xl_fio.open(path);
  if (!ref) {
    xl_say();
    __puts(pstr("can't open "));
    __puts(path);
    __putln();
    return 1;
  }
  n = xl_fio.read(ref, (char *)xl_name, 12);
  xl_fio.close(ref);
  xl_in = (unsigned char *)xl_name;
  xl_end = xl_in + n;
  if (!xl_header())
    return 1;
  zero = a.base + xl_n * 5u + xl_d;     /* its zero region */
  zend = zero + xl_z;
  top = zend + xl_c;                    /* its end */

  /* The second part goes in the program's zero region if it fits there
   * above this module, else above the program; never over this module
   * (its end is the heap's mark). */
  xl_in = xl2_image;
  xl_end = xl2_image + sizeof xl2_image;
  xl_err = 0;
  xl_header();
  need = xl_n * 5u + xl_d + xl_z + xl_c;
  end = (unsigned char *)__heapmark();
  t = zero > end ? zero : end;
  if (t + need > zend)
    t = top > end ? top : end;
  if ((unsigned)(t + need + 512) - (unsigned)end > (unsigned)__heapavail()) {
    xl_say();
    __puts(pstr("not enough memory"));
    __putln();
    return 1;
  }

  /* The second part: relocated there, its zero region cleared (its own
   * init isn't run), and called at its first function. */
  if (!xl_names(0))
    return 1;
  xl_load(&a, t);
  if (xl_err)
    return 1;
  __memset(t + xl_n * 5 + xl_d, 0, xl_z);
  entry = (int (*)(struct xl_args *))(void *)t;
  if (entry(&a))
    return 1;

  /* Under plvm, the program runs here (on an Apple, the second part
   * hands off and never comes back). Only locals from here on: this
   * module's globals and def entries are the program's now. */
  i = ((int (*)(void))(void *)a.init)();
  return -i & 255;
}

/* Capture the VM's state first (4.5), on the Apple, then do the rest:
 * main has no locals, so no frame, and calls the capture routine with
 * nothing on the evaluation stack. */
int main(void)
{
  if (xl_findvm() == XL_APPLE64) {
    xl_spx = ((int (*)(void))(void *)xl_capture)();
    xl_ifp = *(int *)0xE0;
  }
  return xl_run();
}

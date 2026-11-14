/* xload.c: XLOAD's first part (../doc/xload.md, section 4), an
 * ordinary REL module: +XLOAD [-V] PROGRAM ARGS loads PROGRAM, an
 * XLOAD image (cclink -x), and runs it with the command line PROGRAM
 * ARGS. This part is all that PLASMA's loader runs. It finds which VM
 * it's on, captures the VM's state for the hand-off, reads the
 * program's header, and loads the second part (xload2.c, an image in
 * the file XLOAD2, beside this one) to where the program won't load
 * over it, and calls it. The second part loads the program
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

#define XL_FILE
#include "xlcore.c"

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

/* The VM's state when main began (4.5): captured SP and X, IFP and
 * PP. */
static int xl_spx, xl_ifp, xl_pp;

/* Which VM XLOAD is on (4.2), from its own first two def entries: XL_*,
 * or 0 if it's one XLOAD doesn't know. */
static int xl_findvm(void)
{
  unsigned char *e0 = (unsigned char *)xl_probe0;
  unsigned char *e1 = (unsigned char *)xl_probe1;
  int vec = e0[1] | (e0[2] << 8);
  if (e1 - e0 == 5 && vec == XL_VEC_APPLE)
    return XL_APPLE64;
  if (e1 - e0 == 7 && (vec == XL_VEC_AUX || vec == XL_VEC_APPLE))
    return XL_APPLE128;
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

/* On the 128K VM, where n bytes of bytecode can go in aux memory from
 * at, skipping what CMD reserves there, as its allocxheap() does
 * (cmdjit.pla: the 80-column text pages, the double hi-res pages, or
 * super hi-res, by the system flags): the address, or 0 if they don't
 * fit below $A000, where CMD's aux memory ends. */
static unsigned xl_auxfit(unsigned at, unsigned n)
{
  int fl = xl_sys.systemflags;
  if ((fl & 0x0004) && at < 0x0800 && n > 0x0400 - at)
    at = 0x0800;
  if ((fl & 0x0008) && at < 0x0C00 && n > 0x0800 - at)
    at = 0x0C00;
  if (fl & 0x0200) {
    if (at < 0xA000 && n > 0x2000 - at)
      at = 0xA000;
  } else {
    if ((fl & 0x0040) && at < 0x4000 && n > 0x2000 - at)
      at = 0x4000;
    if ((fl & 0x0080) && at < 0x6000 && n > 0x4000 - at)
      at = 0x6000;
  }
  return at < 0xA000 && n <= 0xA000 - at ? at : 0;
}

/* CMD's CALL, as a DCI name, for lookupsym. */
static char xl_callname[] = { 'C' | 0x80, 'A' | 0x80, 'L' | 0x80, 'L' };

/* Report that XLOAD can't open the file name (a PLASMA string), and
 * return 1. */
static int xl_noopen(char *name)
{
  xl_say();
  __cmd_puts(pstr("can't open "));
  __cmd_puts(name);
  __cmd_putln();
  return 1;
}

/* Close the second part's file (XLOAD2), and return 1, for an error. */
static int xl_close2(void)
{
  xl_fio.close(xl_ref);
  xl_ref = 0;
  return 1;
}

/* Load the program and run it; the first part's work after main's
 * capture. Returns main's exit status for XLOAD's own (0 to 255). */
static int xl_run(void)
{
  struct xl_args a;
  char path[17], self[34];
  int i, n, ref, (*entry)(struct xl_args *);
  unsigned char *end, *t, *zero, *zend, *top, *e;
  unsigned need, code, aux2;
  char *cl;

  a.vm = xl_findvm();
  a.esize = a.vm == XL_APPLE128 ? 7 : 5;
  a.vec = a.vm == XL_APPLE64 ? XL_VEC_APPLE
          : a.vm == XL_APPLE128 ? XL_VEC_AUX : XL_VEC_PLVM;
  a.base = (unsigned char *)xl_probe0;
  a.spx = xl_spx;
  a.ifp = xl_ifp;
  a.pp = xl_pp;
  a.init = 0;
  a.aux = 0;
  /* The second part's file, XLOAD2: this one's path, from the command
   * line (without the "+" that CMD's has when it runs XLOAD), with a
   * "2" on the end. */
  cl = xl_sys.cmdline;
  n = *cl;
  i = n > 0 && cl[1] == '+' ? 2 : 1;
  for (ref = 0; i <= n && cl[i] != ' ' && ref < 32; i++)
    self[++ref] = cl[i];
  self[++ref] = '2';
  self[0] = (char)ref;
  a.verbose = xl_args_off();
  /* On the 128K VM, none of XLOAD's own functions may be compiled by
   * the JIT, whose scratch memory is at the heap's mark, inside the
   * program being loaded (xlcore.c, xl_jit): CMD gave this module's
   * small ones its countdown (JSR $03D6), and the plain entry stops
   * it. None has run 32 times yet. */
  if (a.vm == XL_APPLE128)
    for (e = (unsigned char *)xl_probe0; *e == 0x20; e += 7) {
      e[1] = (unsigned char)XL_VEC_AUX;
      e[2] = (unsigned char)(XL_VEC_AUX >> 8);
    }
  if (!a.vm) {
    xl_say();
    __cmd_puts(pstr("doesn't know this VM yet"));
    __cmd_putln();
    return 1;
  }
  cl = xl_sys.cmdline;
  n = *cl;
  for (i = 1; i <= n && cl[i] != ' ' && i <= 16; i++)
    path[i] = cl[i];
  path[0] = (char)(i - 1);
  if (path[0] == 0) {
    __cmd_puts(pstr("usage: +XLOAD [-V] PROGRAM [ARGS]"));
    __cmd_putln();
    return 2;
  }
  a.name = path;

  /* The program's sizes, from its header. */
  ref = xl_fio.open(path);
  if (!ref)
    return xl_noopen(path);
  n = xl_fio.read(ref, (char *)xl_name, 12);
  xl_fio.close(ref);
  xl_in = (unsigned char *)xl_name;
  xl_end = xl_in + n;
  if (!xl_header())
    return 1;
  zero = a.base + xl_n * (unsigned)a.esize + xl_d;  /* its zero region */
  zend = zero + xl_z;
  code = xl_c;
  top = a.vm == XL_APPLE128 ? zend : zend + code;   /* its end */

  /* The second part goes in the program's zero region if it fits there
   * above this module, else above the program; never over this module
   * (its end is the heap's mark). */
  xl_ref = xl_fio.open(self);
  if (!xl_ref)
    return xl_noopen(self);
  xl_in = xl_end = xl_buf;
  if (!xl_header())
    return xl_close2();
  need = xl_n * (unsigned)a.esize + xl_d + xl_z;
  if (a.vm != XL_APPLE128)
    need += xl_c;
  end = (unsigned char *)__cmd_heapmark();
  t = zero > end ? zero : end;
  if (t + need > zend)
    t = top > end ? top : end;
  if ((unsigned)(t + need + 512) - (unsigned)end > (unsigned)__cmd_heapavail()) {
    xl_say();
    __cmd_puts(pstr("not enough memory"));
    __cmd_putln();
    return xl_close2();
  }

  /* On the 128K VM, the bytecode goes in aux memory: the program's
   * where this module's begins (its first def entry's), and the second
   * part's at the top, above both the program's and this module's (its
   * last def entry is init's, whose bytecode is a few bytes, last). */
  if (a.vm == XL_APPLE128) {
    a.call = (int (*)(int, int, int, int, int))xl_sys.lookupsym(xl_callname);
    e = (unsigned char *)xl_probe0;
    while (e[7] == 0x20)
      e += 7;
    a.aux = xl_auxfit(*(unsigned *)((unsigned char *)xl_probe0 + 3), code);
    aux2 = 0xA000 - xl_c;
    if (!a.call || !a.aux || aux2 < a.aux + code
        || aux2 < *(unsigned *)(e + 3) + 16 || xl_auxfit(aux2, xl_c) != aux2) {
      xl_say();
      __cmd_puts(pstr("not enough aux memory"));
      __cmd_putln();
      return xl_close2();
    }
    code = a.aux;               /* the program's, while a.aux is ours */
    a.aux = aux2;
  }

  /* The second part: relocated there, its zero region cleared (its own
   * init isn't run), and called at its first function. */
  if (!xl_names(0))
    return xl_close2();
  xl_load(&a, t);
  xl_close2();
  if (xl_err)
    return 1;
  if (a.vm == XL_APPLE128)
    a.aux = code;
  __cmd_memset(t + xl_n * (unsigned)a.esize + xl_d, 0, xl_z);
  /* Move the heap's mark above the program and the second part while
   * the program loads. On the 128K VM the JIT compiles in scratch
   * memory at the mark (src/libsrc/jitcore.pla), and FILEIO's read,
   * which loading calls dozens of times, comes to be compiled. (The
   * second part sets the mark to the program's end when it's done.) */
  e = t + need > top ? t + need : top;
  end = (unsigned char *)__cmd_heapmark();
  if (e > end && (unsigned)(e - end) <= (unsigned)__cmd_heapavail())
    __cmd_heapalloc((unsigned)(e - end));
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
  if (xl_findvm() >= XL_APPLE64) {
    xl_spx = ((int (*)(void))(void *)xl_capture)();
    xl_ifp = *(int *)0xE0;
    xl_pp = *(int *)0xE2;
  }
  return xl_run();
}

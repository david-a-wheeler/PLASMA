/* xlcore.c: what both parts of XLOAD share (../doc/xload.md, section
 * 4): reading an XLOAD image (section 3) from a byte source, building
 * its def entries for the VM XLOAD is on, and relocating it. Included,
 * not linked: xload.c and xload2.c are programs of their own, and each
 * includes this once. The includer defines XL_FILE if its source may be
 * a file (the second part), not only memory (the first). */

/* CMDSYS's table, as far as XLOAD reads it (src/inc/cmdsys.plh; the
 * same offsets on the Apple and under plvm, vmsrc/cmdsys.c). What XLOAD
 * doesn't use is padding, since ccish allows a file only 32 struct
 * members in all (MAX_MEMBERS). */
struct xl_cmdsys {
  char pad0[4];               /* sysver, syspath */
  char *cmdline;
  char pad1[11];              /* modexec, open, close, read, write,
                                 syserr */
  unsigned char jitcount;
  unsigned char jitsize;
  char pad2[2];               /* execsys */
  int (*lookupsym)(char *);
  int systemflags;            /* the 128K VM's: aux memory reserved */
};
extern struct xl_cmdsys xl_sys __asm__("CMDSYS");

/* FILEIO's table, as far as XLOAD uses it (src/inc/fileio.plh). */
#pragma plasma import FILEIO
struct xl_fileio {
  char pad[14];               /* getpfx to iobufalloc */
  int (*open)(char *);
  int (*close)(int);
  int (*read)(int, char *, int);
};
extern struct xl_fileio xl_fio __asm__("FILEIO");

/* The VMs XLOAD knows (4.2), and the def entry vectors it finds and
 * builds. */
#define XL_PLVM     1
#define XL_APPLE64  2
#define XL_APPLE128 3
#define XL_VEC_APPLE 0x03D6   /* the 64K Apple's indirect entry; the
                                 128K one's for its JIT */
#define XL_VEC_AUX  0x03DC    /* the 128K Apple's: bytecode in aux */
#define XL_VEC_PLVMX 0xFF0C   /* plvm's entry for a module's bytecode */
#define XL_VEC_PLVM  0xFF08   /* plvm's entry for bytecode in 6502 memory */

/* What the first part passes the second (and the second hands back):
 * in the first part's frame, which nothing loads over. */
struct xl_args {
  int vm;                     /* XL_* */
  int esize;                  /* a def entry's bytes */
  int vec;                    /* its vector */
  unsigned char *base;        /* where the program goes */
  char *name;                 /* its file, a PLASMA string */
  int verbose;                /* -V */
  int spx;                    /* captured: SP (high) and X (low) */
  int ifp, pp;                /* captured: the frame pointers */
  int init;                   /* back: the program's init entry */
  unsigned aux;               /* the 128K VM: where the bytecode goes in
                                 aux memory; else 0, after the zeros */
  int (*call)(int, int, int, int, int); /* the 128K VM: CMD's CALL */
};

#define XL_MAXIMP 127       /* imports one image may have */

/* The image's sizes, from its header: its initialized data, zero
 * region, bytecode and functions. */
static int xl_d, xl_z, xl_c, xl_n;

/* The imports' addresses, by number, and how many there are. */
static int xl_imp[XL_MAXIMP];
static int xl_nimp;

/* A DCI name, as read from the image. */
static char xl_name[17];

/* The fixups of each kind applied: the load address, a def entry, an
 * import. */
static int xl_nadd, xl_ndef, xl_nimpfix;

/* Set when something has gone wrong (xl_fail()). */
static int xl_err;

/* The source: its bytes not yet read, and, for a file, its reference
 * number and buffer. At the end, or after an error, it gives $FF bytes
 * (xl_ff), which end the fixups. */
static unsigned char *xl_in, *xl_end;
static unsigned char xl_ff[1] = { 0xFF };
#ifdef XL_FILE
#define XL_BUF 512
static int xl_ref;
static unsigned char xl_buf[XL_BUF];
#endif

/* On the 128K VM, a window on the bytecode in aux memory: XL_WIN bytes
 * at most, xl_wn of them, from bytecode offset xl_ws. Loading moves the
 * bytecode through it to aux, and the fixups patch it there. */
#define XL_WIN 128
static unsigned char xl_win[XL_WIN];
static unsigned xl_ws, xl_wn;

/* Set to give the def entries xl_defs() builds the 128K VM's JIT
 * countdown: the program's, never XLOAD's own. The JIT compiles in
 * scratch memory at the heap's mark (src/libsrc/jitcore.pla), which
 * while XLOAD runs is inside the program being loaded. */
static int xl_jit;

/* Print XLOAD's prefix for a message. */
static void xl_say(void)
{
  __cmd_puts(pstr("XLOAD: "));
}

/* Report what's wrong (only the first time), and stop the source. */
static void xl_fail(char *msg)
{
  if (!xl_err) {
    xl_say();
    __cmd_puts(msg);
    __cmd_putln();
  }
  xl_err = 1;
  xl_in = xl_ff;
  xl_end = xl_ff + 1;
}

/* Refill the source, or fail at its end. */
static void xl_fill(void)
{
#ifdef XL_FILE
  int n;
  if (xl_ref && !xl_err && (n = xl_fio.read(xl_ref, (char *)xl_buf, XL_BUF)) > 0) {
    xl_in = xl_buf;
    xl_end = xl_buf + n;
    return;
  }
#endif
  xl_fail(pstr("the image is cut short"));
}

/* The source's next byte. */
static int xl_get(void)
{
  if (xl_in == xl_end)
    xl_fill();
  return *xl_in++;
}

/* The source's next word. */
static int xl_getw(void)
{
  int lo = xl_get();
  return lo | (xl_get() << 8);
}

/* Read n bytes from the source to to. */
static void xl_read(unsigned char *to, unsigned n)
{
  unsigned k;
  while (n > 0 && !xl_err) {
    if (xl_in == xl_end)
      xl_fill();
    k = (unsigned)(xl_end - xl_in);
    if (k > n)
      k = n;
    __cmd_memcpy(to, xl_in, k);
    xl_in += k;
    to += k;
    n -= k;
  }
}

/* Read the image's header: 1 if it's an image XLOAD can load. */
static int xl_header(void)
{
  if (xl_get() != 'X' || xl_get() != 'L' || xl_get() != 4) {
    xl_fail(pstr("not an XLOAD image, version 4"));
    return 0;
  }
  xl_get();                     /* flags: none yet */
  xl_d = xl_getw();
  xl_z = xl_getw();
  xl_c = xl_getw();
  xl_n = xl_getw();
  return !xl_err && xl_n > 0;
}

/* Read a DCI name into xl_name, as a DCI string again; return 0 at
 * the list's end (a $00). */
static int xl_getname(void)
{
  int i = 0, c = xl_get();
  if (c == 0)
    return 0;
  for (;;) {
    if (i < 16)
      xl_name[i++] = (char)c;
    if (!(c & 0x80) || xl_err)
      break;
    c = xl_get();
  }
  xl_name[i - 1] &= 0x7F;     /* a name cut at 16 still ends */
  return 1;
}

/* Print xl_name, a DCI string. */
static void xl_putname(void)
{
  int i = 0;
  do
    __cmd_putc(xl_name[i] & 0x7F);
  while (xl_name[i++] & 0x80);
}

/* Check the image's dependencies, and look up its imports: 1 if all
 * are there. With verbose, print each import's address. */
static int xl_names(int verbose)
{
  int a;
  while (xl_getname())
    if (!xl_sys.lookupsym(xl_name)) {
      xl_say();
      __cmd_puts(pstr("needs the module "));
      xl_putname();
      __cmd_putln();
      return 0;
    }
  xl_nimp = 0;
  while (xl_getname()) {
    a = xl_sys.lookupsym(xl_name);
    if (!a || xl_nimp == XL_MAXIMP) {
      xl_say();
      __cmd_puts(pstr("can't import "));
      xl_putname();
      __cmd_putln();
      return 0;
    }
    if (verbose) {
      xl_say();
      __cmd_puts(pstr("import "));
      xl_putname();
      __cmd_puts(pstr(" $"));
      __cmd_puth(a);
      __cmd_putln();
    }
    xl_imp[xl_nimp++] = a;
  }
  return !xl_err;
}

/* Copy n bytes from main memory at m to aux memory at x, or the other
 * way if !to_aux: the ROM's XMOVE ($C311), through CMD's CALL, as CMD's
 * loader does (cmdjit.pla). */
static void xl_xmove(struct xl_args *a, unsigned char *m, unsigned x,
                     unsigned n, int to_aux)
{
  unsigned from = to_aux ? (unsigned)m : x;
  *(unsigned *)0x3C = from;     /* A1, A2: the first and last bytes */
  *(unsigned *)0x3E = from + n - 1;
  *(unsigned *)0x42 = to_aux ? x : (unsigned)m;   /* A4: where to */
  a->call(0xC311, 0, 0, 0, to_aux ? 5 : 4);       /* carry: to aux */
}

/* Build the image's def entries at defs, of a->esize bytes each with
 * vector a->vec, from its function table, for a body at body (or on
 * the 128K VM, bytecode at a->aux). The 128K VM's 7-byte entries also
 * get the JIT's countdown with xl_jit, for a function no bigger than
 * its jitsize, as CMD's loader gives them (cmdjit.pla, adddef(); never
 * init's, the last). */
static void xl_defs(struct xl_args *a, unsigned char *defs,
                    unsigned char *body)
{
  int i, off, v, last = 0;
  unsigned dz = (unsigned)xl_d + xl_z, size = dz + xl_c;
  for (i = 0; i < xl_n; i++) {
    off = xl_getw();
    if ((unsigned)off >= size || (a->aux && (unsigned)off < dz))
      xl_fail(pstr("a function outside the image"));
    v = a->aux ? (int)(a->aux + off - dz) : (int)(body + off);
    defs[0] = 0x20;             /* JSR vector, then the address */
    defs[1] = (unsigned char)a->vec;
    defs[2] = (unsigned char)(a->vec >> 8);
    defs[3] = (unsigned char)v;
    defs[4] = (unsigned char)(v >> 8);
    if (a->esize == 7) {
      defs[5] = defs[6] = 0;
      v = off - last;           /* the entry before: its size */
      if (xl_jit && i && (unsigned)v <= xl_sys.jitsize) {
        defs[-6] = (unsigned char)XL_VEC_APPLE;
        defs[-5] = (unsigned char)(XL_VEC_APPLE >> 8);
        defs[-2] = xl_sys.jitcount;
        defs[-1] = (unsigned char)v;
      }
    }
    last = off;
    defs += a->esize;
  }
}

/* Move the bytecode out of the window, back to aux memory. */
static void xl_flush(struct xl_args *a)
{
  if (xl_wn)
    xl_xmove(a, xl_win, a->aux + xl_ws, xl_wn, 1);
  xl_wn = 0;
}

/* Where the word at body offset off is, in main memory: in the body,
 * or on the 128K VM, for bytecode, in the window, which moves to it
 * (the fixups come in address order). */
static unsigned char *xl_at(struct xl_args *a, unsigned char *body,
                            unsigned off)
{
  unsigned c = off - xl_d - xl_z;
  if (!a->aux || off < (unsigned)xl_d + xl_z)
    return body + off;
  if (c + 2 > xl_ws + xl_wn) {
    xl_flush(a);
    xl_ws = c;
    xl_wn = xl_c - c < XL_WIN ? xl_c - c : XL_WIN;
    xl_xmove(a, xl_win, a->aux + c, xl_wn, 0);
  }
  return xl_win + (c - xl_ws);
}

/* Read the bytecode to at, or on the 128K VM, through the window to
 * aux memory. */
static void xl_code(struct xl_args *a, unsigned char *at)
{
  unsigned o, k;
  if (!a->aux) {
    xl_read(at, (unsigned)xl_c);
    return;
  }
  for (o = 0; o < (unsigned)xl_c && !xl_err; o += k) {
    k = xl_c - o < XL_WIN ? xl_c - o : XL_WIN;
    xl_read(xl_win, k);
    xl_xmove(a, xl_win, a->aux + o, k, 1);
  }
}

/* Apply the image's fixups to its body at body (its bytecode at a->aux
 * on the 128K VM), its def entries at defs. */
static void xl_fixups(struct xl_args *a, unsigned char *body,
                      unsigned char *defs)
{
  unsigned char *p;
  unsigned off = 0, end = (unsigned)xl_d + xl_z + xl_c - 1;
  int b, k, v;
  unsigned gap;
  xl_nadd = xl_ndef = xl_nimpfix = 0;
  while ((b = xl_get()) != 0xFF) {
    if (b & 0x80) {             /* an import: its number, then the gap */
      k = b - 0x80;
      b = xl_get();
      gap = b == 0xFF ? (unsigned)xl_getw() : (unsigned)b;
    } else {                    /* $40: a def entry; else the address */
      k = b & 0x40 ? -2 : -1;
      b &= 0x3F;
      gap = b == 0x3F ? (unsigned)xl_getw() : (unsigned)b;
    }
    if (off >= end || gap >= end - off || k >= xl_nimp) {
      xl_fail(pstr("a bad fixup"));
      return;
    }
    off += gap;
    p = xl_at(a, body, off);
    v = p[0] | (p[1] << 8);
    if (k == -1) {
      /* Never an address in the bytecode, which on the 128K VM isn't
       * in main memory: cclink reaches functions through their def
       * entries. (The word may look like one: an address before the
       * data, such as arr - 58, wraps around.) */
      v += (int)body;
      xl_nadd++;
    } else if (k == -2) {
      if ((unsigned)v >= (unsigned)xl_n) {
        xl_fail(pstr("a bad function number"));
        return;
      }
      v = (int)defs + v * a->esize;
      xl_ndef++;
    } else {
      v += xl_imp[k];
      xl_nimpfix++;
    }
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    off += 2;
  }
  if (a->aux)
    xl_flush(a);
}

/* Load the image from the source, its header already read: its def
 * entries at defs, its body after them (the zero region left as it
 * is; on the 128K VM, the bytecode in aux memory at a->aux). Returns its init entry's address (the last function's). */
static int xl_load(struct xl_args *a, unsigned char *defs)
{
  unsigned char *body = defs + xl_n * a->esize;
  xl_defs(a, defs, body);
  xl_read(body, (unsigned)xl_d);
  xl_code(a, body + xl_d + xl_z);
  xl_fixups(a, body, defs);
  return (int)(defs + (xl_n - 1) * a->esize);
}

/* xlcore.c: what both parts of XLOAD share (../doc/xload.md, section
 * 4): reading an XLOAD image (section 3) from a byte source, building
 * its def entries for the VM XLOAD is on, and relocating it. Included,
 * not linked: xload.c and xload2.c are programs of their own, and each
 * includes this once. The includer defines XL_FILE if its source may be
 * a file (the second part), not only memory (the first). */

/* CMDSYS's table, as far as XLOAD reads it (src/inc/cmdsys.plh; the
 * same offsets on the Apple and under plvm, vmsrc/cmdsys.c). */
struct xl_cmdsys {
  int sysver;
  char *syspath;
  char *cmdline;
  void *modexec;
  void *sysopen;
  void *sysclose;
  void *sysread;
  void *syswrite;
  char syserr;
  unsigned char jitcount;
  unsigned char jitsize;
  void *execsys;
  int (*lookupsym)(char *);
};
extern struct xl_cmdsys xl_sys __asm__("CMDSYS");

/* FILEIO's table, as far as XLOAD uses it (src/inc/fileio.plh). */
#pragma plasma import FILEIO
struct xl_fileio {
  void *getpfx;
  void *setpfx;
  void *getfileinfo;
  void *setfileinfo;
  void *geteof;
  void *seteof;
  void *iobufalloc;
  int (*open)(char *);
  int (*close)(int);
  int (*read)(int, char *, int);
};
extern struct xl_fileio xl_fio __asm__("FILEIO");

/* The VMs XLOAD knows (4.2), and the def entry vectors it finds and
 * builds. */
#define XL_PLVM     1
#define XL_APPLE64  2
#define XL_VEC_APPLE 0x03D6   /* the 64K Apple's indirect entry */
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
  int ifp;                    /* captured: the frame pointer */
  int init;                   /* back: the program's init entry */
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

/* Print XLOAD's prefix for a message. */
static void xl_say(void)
{
  __puts(pstr("XLOAD: "));
}

/* Report what's wrong (only the first time), and stop the source. */
static void xl_fail(char *msg)
{
  if (!xl_err) {
    xl_say();
    __puts(msg);
    __putln();
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
    __memcpy(to, xl_in, k);
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
    __putc(xl_name[i] & 0x7F);
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
      __puts(pstr("needs the module "));
      xl_putname();
      __putln();
      return 0;
    }
  xl_nimp = 0;
  while (xl_getname()) {
    a = xl_sys.lookupsym(xl_name);
    if (!a || xl_nimp == XL_MAXIMP) {
      xl_say();
      __puts(pstr("can't import "));
      xl_putname();
      __putln();
      return 0;
    }
    if (verbose) {
      xl_say();
      __puts(pstr("import "));
      xl_putname();
      __puts(pstr(" $"));
      __puth(a);
      __putln();
    }
    xl_imp[xl_nimp++] = a;
  }
  return !xl_err;
}

/* Build the image's def entries at defs, of a->esize bytes each with
 * vector a->vec, from its function table, for a body at body. */
static void xl_defs(struct xl_args *a, unsigned char *defs,
                    unsigned char *body)
{
  int i, off, v;
  unsigned size = (unsigned)xl_d + xl_z + xl_c;
  for (i = 0; i < xl_n; i++) {
    off = xl_getw();
    if ((unsigned)off >= size)
      xl_fail(pstr("a function outside the image"));
    v = (int)(body + off);
    defs[0] = 0x20;             /* JSR vector, then the address */
    defs[1] = (unsigned char)a->vec;
    defs[2] = (unsigned char)(a->vec >> 8);
    defs[3] = (unsigned char)v;
    defs[4] = (unsigned char)(v >> 8);
    defs += a->esize;
  }
}

/* Apply the image's fixups to its body at body, its def entries at
 * defs, of esize bytes each. */
static void xl_fixups(unsigned char *body, unsigned char *defs, int esize)
{
  unsigned char *p = body, *end = body + xl_d + xl_z + xl_c - 1;
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
    if (gap >= (unsigned)(end - p) || k >= xl_nimp) {
      xl_fail(pstr("a bad fixup"));
      return;
    }
    p += gap;
    v = p[0] | (p[1] << 8);
    if (k == -1) {
      v += (int)body;
      xl_nadd++;
    } else if (k == -2) {
      if ((unsigned)v >= (unsigned)xl_n) {
        xl_fail(pstr("a bad function number"));
        return;
      }
      v = (int)defs + v * esize;
      xl_ndef++;
    } else {
      v += xl_imp[k];
      xl_nimpfix++;
    }
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p += 2;
  }
}

/* Load the image from the source, its header already read: its def
 * entries at defs, its body after them (the zero region left as it
 * is). Returns its init entry's address (the last function's). */
static int xl_load(struct xl_args *a, unsigned char *defs)
{
  unsigned char *body = defs + xl_n * a->esize;
  xl_defs(a, defs, body);
  xl_read(body, (unsigned)xl_d);
  xl_read(body + xl_d + xl_z, (unsigned)xl_c);
  xl_fixups(body, defs, a->esize);
  return (int)(defs + (xl_n - 1) * a->esize);
}

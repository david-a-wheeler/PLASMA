/* xload2.c: XLOAD's second part (../doc/xload.md, section 4), built
 * as an XLOAD image (ccrel -x) and held in the first part (xload.c)
 * as data. The first part copies it to where the program won't load
 * over it, relocates it, and calls xl2_entry(), its first function.
 * It loads the program, in place at the bottom of the heap, and hands
 * off to it: on the Apple, straight into the program's init, as if
 * PLASMA's loader had called it; under plvm, back to the first part,
 * which calls it. */

/* The second part's entry: its first function, so the first in its
 * function table, where the first part calls it. Returns 0 when the
 * program is loaded (a->init set, under plvm), else 1. */
struct xl_args;
static int xl2_load(struct xl_args *a);

/* The entry (xl2_load() does the work). */
int xl2_entry(struct xl_args *a)
{
  return xl2_load(a);
}

#define XL_FILE
#include "xlcore.c"

/* The hand-off routine (4.5), with its operands filled in: restore the
 * frame pointer, the 6502 stack pointer and the evaluation stack index
 * as they were when the program's init would have begun, and jump to
 * it. */
static unsigned char xl_handoff[] = {
  0xA9, 0,                    /* LDA #<IFP */
  0x85, 0xE0,                 /* STA IFPL */
  0xA9, 0,                    /* LDA #>IFP */
  0x85, 0xE1,                 /* STA IFPH */
  0xA2, 0,                    /* LDX #SP */
  0x9A,                       /* TXS */
  0xA2, 0,                    /* LDX #X */
  0x4C, 0, 0                  /* JMP init */
};

/* An init for when loading fails on the Apple, after the first part
 * is gone: a def entry (JSR $03D6 and the address of the bytecode
 * after it), then MINUS1 RET, so that PLASMA's loader reports an
 * error. */
static unsigned char xl_stub[] = {
  0x20, 0xD6, 0x03, 0, 0,
  0x20,                       /* MINUS1 */
  0x5C                        /* RET */
};

/* With -V, print a label and a number, in hex if hex. */
static void xl_show(char *label, int v, int hex)
{
  __puts(label);
  if (hex) {
    __putc('$');
    __puth(v);
  } else {
    __puti(v);
  }
}

/* Load the program the first part's a names, at a->base. */
static int xl2_load(struct xl_args *a)
{
  unsigned char *defs = a->base, *body, *top;
  unsigned total;

  xl_ref = xl_fio.open(a->name);
  if (!xl_ref) {
    xl_say();
    __puts(pstr("can't open "));
    __puts(a->name);
    __putln();
    return 1;
  }
  xl_in = xl_end = xl_buf;
  if (!xl_header() || !xl_names(a->verbose)) {
    xl_fio.close(xl_ref);
    return 1;
  }
  total = xl_n * (unsigned)a->esize + xl_d + xl_z + xl_c;
  body = defs + xl_n * a->esize;
  top = defs + total;
  /* The program, and 512 bytes for its frames (as a REL program's
   * init checks), must fit below the frame stack. */
  if ((unsigned)top + 512 < (unsigned)top
      || (unsigned)top + 512
         > (unsigned)__heapmark() + (unsigned)__heapavail()) {
    xl_say();
    __puts(pstr("not enough memory"));
    __putln();
    xl_fio.close(xl_ref);
    return 1;
  }
  if (a->verbose) {
    xl_say();
    if (a->vm == XL_PLVM)
      __puts(pstr("VM plvm"));
    else
      __puts(pstr("VM Apple 64K"));
    xl_show(pstr(", entries JSR "), a->vec, 1);
    __putln();
    xl_say();
    xl_show(pstr("data "), xl_d, 0);
    xl_show(pstr(", zero "), xl_z, 0);
    xl_show(pstr(", code "), xl_c, 0);
    xl_show(pstr(", functions "), xl_n, 0);
    xl_show(pstr(", imports "), xl_nimp, 0);
    __putln();
    xl_say();
    xl_show(pstr("defs "), (int)defs, 1);
    xl_show(pstr(", body "), (int)body, 1);
    xl_show(pstr(", zero "), (int)(body + xl_d), 1);
    xl_show(pstr(", code "), (int)(body + xl_d + xl_z), 1);
    xl_show(pstr(", end "), (int)top, 1);
    xl_show(pstr("; XLOAD2 "), (int)xl_handoff, 1);
    __putln();
  }

  /* From here on, the first part's memory is the program's. */
  a->init = xl_load(a, defs);
  xl_fio.close(xl_ref);
  xl_ref = 0;
  if (a->verbose) {
    xl_say();
    xl_show(pstr("fixups "), xl_nadd + xl_ndef + xl_nimpfix, 0);
    xl_show(pstr(": address "), xl_nadd, 0);
    xl_show(pstr(", function "), xl_ndef, 0);
    xl_show(pstr(", import "), xl_nimpfix, 0);
    __putln();
    xl_say();
    xl_show(pstr("init "), a->init, 1);
    xl_show(pstr(", free "),
            (int)((unsigned)__heapmark() + (unsigned)__heapavail()
                  - (unsigned)top), 1);
    if (a->vm == XL_APPLE64) {
      xl_show(pstr("; hand-off SP "), (a->spx >> 8) & 255, 1);
      xl_show(pstr(", X "), a->spx & 255, 1);
      xl_show(pstr(", IFP "), a->ifp, 1);
    } else {
      __puts(pstr("; a plain call"));
    }
    __putln();
  }
  if (xl_err) {
    if (a->vm != XL_APPLE64)
      return 1;                 /* the first part is still there */
    total = (unsigned)(xl_stub + 5);
    xl_stub[3] = (unsigned char)total;
    xl_stub[4] = (unsigned char)(total >> 8);
    a->init = (int)xl_stub;
  } else {
    __heaprelease(top);
  }
  if (a->vm == XL_APPLE64) {
    /* The hand-off: the capture was 8 bytes below init's stack. */
    xl_handoff[1] = (unsigned char)a->ifp;
    xl_handoff[5] = (unsigned char)(a->ifp >> 8);
    xl_handoff[9] = (unsigned char)((a->spx >> 8) + 8);
    xl_handoff[12] = (unsigned char)a->spx;
    xl_handoff[14] = (unsigned char)a->init;
    xl_handoff[15] = (unsigned char)(a->init >> 8);
    ((void (*)(void))(void *)xl_handoff)();
  }
  return 0;
}

/* An image needs main; the first part calls xl2_entry() instead. */
int main(void)
{
  return 0;
}

/* exit.c: C's exit (doc/bootstrap-test.md, R8 and step 14). On an Apple II (the 64K VM or the 128K one), it
 * closes the open files and unwinds to the program's init, which
 * returns -(status & 255) to CMD, as if main had returned status.
 * ccrel's init calls __exitsave() just before main, when exit is
 * linked; __exitsave() runs a native routine that saves what the
 * unwind needs: the 6502 stack pointer, the evaluation stack index
 * (X), the frame pointers IFP and PP ($E0-$E3, the same on both VMs),
 * and the 16 stack bytes above the stack pointer, which hold its own
 * return into the VM and init's call of it, and which main's call
 * overwrites. exit's native routine puts them back and returns, so
 * that __exitsave() returns a second time, with (status & 255) | 256.
 *
 * Under plvm the VM's stacks are C variables, out of a native
 * routine's reach, so there exit prints tools/ccstat.pla's marker with
 * the status (tools/ccrun reports it) and runs a 6502 BRK, which stops
 * plvm. */
#include <stdlib.h>
#include <fileio.h>

/* The two routines, and the save area after them. Each is called
 * through ICAL, which leaves its address in TMP ($E7), so each reaches
 * the area through (TMP),Y, at its own offset: RST_AREA for rst, at 0,
 * and CAP_AREA for cap, at CAP. The area holds the stack pointer, X,
 * IFP and PP, the 16 stack bytes, and the value __exitsave() returns
 * after exit. */
#define CAP       51
#define CAP_AREA  48
#define RST_AREA  (CAP + CAP_AREA)
#define SV_X      1
#define SV_FP     2
#define SV_STACK  6
#define SV_VALUE  22
static unsigned char xr[RST_AREA + 24] = {
  /* rst, exit's: put back what cap saved and push the value, so that
   * it returns as cap did, into __exitsave(). */
  0xA0, RST_AREA,               /* LDY #SP */
  0xB1, 0xE7,                   /* LDA (TMP),Y */
  0xAA, 0x9A,                   /* TAX, TXS */
  0xA0, RST_AREA + SV_STACK,    /* LDY #STACK */
  0xE8,                         /* loop: INX */
  0xF0, 0x0A,                   /* BEQ fp (the stack's top) */
  0xB1, 0xE7,                   /* LDA (TMP),Y */
  0x9D, 0x00, 0x01,             /* STA $0100,X */
  0xC8,                         /* INY */
  0xC0, RST_AREA + SV_VALUE,    /* CPY #VALUE */
  0xD0, 0xF3,                   /* BNE loop */
  0xA0, RST_AREA + SV_FP,       /* fp: LDY #FP */
  0xB1, 0xE7,                   /* fpl: LDA (TMP),Y */
  0x99, 0xE0 - (RST_AREA + SV_FP), 0x00, /* STA $E0-FP,Y: IFP, PP */
  0xC8,                         /* INY */
  0xC0, RST_AREA + SV_STACK,    /* CPY #STACK */
  0xD0, 0xF6,                   /* BNE fpl */
  0xA0, RST_AREA + SV_X,        /* LDY #X */
  0xB1, 0xE7, 0xAA, 0xCA,       /* LDA (TMP),Y, TAX, DEX */
  0xA0, RST_AREA + SV_VALUE,    /* LDY #VALUE */
  0xB1, 0xE7, 0x95, 0xC0,       /* LDA (TMP),Y, STA ESTKL,X */
  0xC8,                         /* INY */
  0xB1, 0xE7, 0x95, 0xA0,       /* LDA (TMP),Y, STA ESTKH,X */
  0x60,                         /* RTS */
  /* cap, at CAP: save, and push 0. */
  0x8A,                         /* TXA */
  0xA0, CAP_AREA + SV_X,        /* LDY #X */
  0x91, 0xE7,                   /* STA (TMP),Y */
  0xBA, 0x8A,                   /* TSX, TXA */
  0x88,                         /* DEY: SP */
  0x91, 0xE7,                   /* STA (TMP),Y */
  0xA0, CAP_AREA + SV_FP,       /* LDY #FP */
  0xB9, 0xE0 - (CAP_AREA + SV_FP), 0x00, /* fpl: LDA $E0-FP,Y */
  0x91, 0xE7,                   /* STA (TMP),Y */
  0xC8,                         /* INY */
  0xC0, CAP_AREA + SV_STACK,    /* CPY #STACK */
  0xD0, 0xF6,                   /* BNE fpl */
  0xE8,                         /* loop: INX */
  0xF0, 0x0A,                   /* BEQ done (the stack's top) */
  0xBD, 0x00, 0x01,             /* LDA $0100,X */
  0x91, 0xE7,                   /* STA (TMP),Y */
  0xC8,                         /* INY */
  0xC0, CAP_AREA + SV_VALUE,    /* CPY #VALUE */
  0xD0, 0xF3,                   /* BNE loop */
  0xA0, CAP_AREA + SV_X,        /* done: LDY #X */
  0xB1, 0xE7, 0xAA, 0xCA,       /* LDA (TMP),Y, TAX, DEX */
  0xA9, 0x00,                   /* LDA #0 */
  0x95, 0xC0, 0x95, 0xA0,       /* STA ESTKL,X, STA ESTKH,X */
  0x60                          /* RTS */
};

/* Set once __exitsave() has saved, on an Apple II. */
static int saved;

/* Called by init just before main: 0, or after exit, its status & 255
 * with 256 added. On an Apple II only: a function's address is its def
 * entry, a JSR to the VM, $03xx there and $FFxx under plvm. It has no
 * frame, so that nothing of its own is in memory that main reuses, but
 * the stack bytes cap saves. */
int __exitsave(void)
{
  if (((unsigned char *)exit)[2] != 0x03)
    return 0;
  saved = 1;
  return ((int (*)(void))(void *)(xr + CAP))();
}

/* A BRK instruction: its opcode is 0. */
static char brk[1];

/* End the program with status. */
void exit(int status)
{
  int r;
  if (saved) {
    for (r = 1; r <= 8; r++)    /* ProDOS's reference numbers */
      fileio.close(r);
    xr[RST_AREA + SV_VALUE] = (unsigned char)status;
    xr[RST_AREA + SV_VALUE + 1] = 1;
    ((void (*)(void))(void *)xr)();
  }
  __putc(1);
  __puts(pstr("exit "));
  __puti(status & 255);
  __putln();
  ((void (*)(void))brk)();
}

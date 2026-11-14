/* exit.c: C's exit, on an Apple II, the 64K VM or the 128K one, and
 * under plvm (doc/bootstrap-test.md, R8 and step 14; doc/linking.md,
 * 5.5). On an Apple II it makes main return
 * status, from wherever exit was called, so that init returns
 * -(status & 255) to CMD. When exit is linked, cclink's init runs
 * _exitcap's first routine (cap) just before it calls main. On both
 * VMs, ICAL (or ICALX) and CALL (or CALLX) push the same frame, the
 * caller's IP and a JSR's return, so cap runs at the very stack pointer
 * main runs at, and main's call frame stays intact above it until main
 * returns. cap saves that stack pointer, the evaluation stack index (X)
 * and the frame pointers IFP and PP ($E0-$E3, the same on both VMs).
 * exit runs the second routine (rst), which puts them back, with
 * status on the evaluation stack, and runs an RTS, which returns from
 * main's call, as main's RET would. (The 128K VM's CALLX and ICALX
 * both put the caller's status register back before they call, so
 * rst's RTS has the flags RETX would have given it.)
 *
 * exit closes no files: CMD closes them all after each command it runs
 * with "+", as XLOAD is run.
 *
 * Under plvm, whose stacks are C variables, out of a native routine's
 * reach, exit can't unwind: it prints tools/ccstat.pla's marker, ^A
 * and the status (tools/ccrun reads it), and runs a 6502 BRK, which
 * stops plvm. cap still runs there, on plvm's emulated 6502, writing
 * only its own area. (plvm's ICAL doesn't set TMP, so init sets it
 * before calling cap, on both.) A stopgap, until plvm can unwind or
 * exit with a status (doc/plasma-requests.md, items 2 and 6). */
#include <stdlib.h>

/* cap, then rst, then their save area. Each routine is called through
 * ICAL, which leaves its address in TMP ($E7) on the Apple VMs (and init
 * sets TMP itself before calling cap), so each reaches the area
 * through (TMP),Y, at its own offset: AREA for cap, at 0, and AREA - RST
 * for rst. The area holds the stack pointer, X, and IFP and PP. */
#define RST       25
#define AREA      63
#define RST_AREA  (AREA - RST)
#define SV_SP     0
#define SV_X      1
#define SV_FP     2
#define SV_END    6
/* A 0 byte in cap, its LDA's address's high byte: a BRK, for plvm. */
#define BRK_AT    4
unsigned char _exitcap[AREA + SV_END] = {
  /* cap: save IFP, PP, X and the stack pointer. */
  0xA0, AREA + SV_END - 1,         /* LDY #FP+3 */
  0xB9, 0xE0 - (AREA + SV_FP), 0x00, /* fp: LDA $E0-FP,Y (BRK_AT: 0) */
  0x91, 0xE7,                      /* STA (TMP),Y */
  0x88,                            /* DEY */
  0xC0, AREA + SV_X,               /* CPY #X */
  0xD0, 0xF6,                      /* BNE fp */
  0x8A, 0x91, 0xE7,                /* TXA, STA (TMP),Y: X */
  0x88,                            /* DEY: SP */
  0xBA, 0x8A, 0x91, 0xE7,          /* TSX, TXA, STA (TMP),Y */
  0xC8,                            /* INY: X */
  0xB1, 0xE7, 0xAA,                /* LDA (TMP),Y, TAX */
  0x60,                            /* RTS */
  /* rst, at RST: take the status from the evaluation stack, put back
   * what cap saved, with the status on top, and return from main. */
  0xB5, 0xC0, 0x48,                /* LDA ESTKL,X, PHA */
  0xB5, 0xA0, 0x48,                /* LDA ESTKH,X, PHA */
  0xA0, RST_AREA + SV_END - 1,     /* LDY #FP+3 */
  0xB1, 0xE7,                      /* fp: LDA (TMP),Y */
  0x99, 0xE0 - (RST_AREA + SV_FP), 0x00, /* STA $E0-FP,Y */
  0x88,                            /* DEY */
  0xC0, RST_AREA + SV_X,           /* CPY #X */
  0xD0, 0xF6,                      /* BNE fp */
  0xB1, 0xE7, 0xAA, 0xCA,          /* LDA (TMP),Y, TAX, DEX */
  0x68, 0x95, 0xA0,                /* PLA, STA ESTKH,X */
  0x68, 0x95, 0xC0,                /* PLA, STA ESTKL,X */
  0x88,                            /* DEY: SP */
  0xB1, 0xE7,                      /* LDA (TMP),Y */
  0x86, 0xE7,                      /* STX TMPL (done with TMP) */
  0xAA, 0x9A,                      /* TAX, TXS */
  0xA6, 0xE7,                      /* LDX TMPL */
  0x60                             /* RTS, from main's call */
};

/* Nonzero on an Apple II, 0 under plvm: on both Apple VMs, $F1 is the
 * opcode of the fetch loop's LDA IP,Y (FETCHOP, $B9; plvm02.s and
 * plvmjit02.s copy the loop to $EF), in zero page; plvm's zero page is 0
 * there (its IP is a C variable). Cheaper than a test of a def entry. */
#define ON_APPLE  (*(unsigned char *)0xF1)

/* End the program with status, as if main had returned it. */
void exit(int status)
{
  if (!ON_APPLE) {
    __cmd_putc(1);
    __cmd_puti(status);
    ((void (*)(void))(void *)(_exitcap + BRK_AT))();
  }
  ((void (*)(int))(void *)(_exitcap + RST))(status);
}

/* ptest.h — run one program under ccish and under a host C compiler
 * (gcc or clang), and expect the same answer.
 *
 * A program that includes this header and prints only through puti,
 * putc, putln, puts and pstr will build and run under both, provided
 * it honors the portability contract:
 *
 *   - The ccish dialect (approach.md decision 29): a subset of C89
 *     plus ccish's own small extensions - int, char, pointers,
 *     arrays, structs and functions. No long, float or double
 *     (ccish has none yet). A test that uses a ccish extension must
 *     also compile under the host compiler; the cross check is the
 *     gate.
 *   - Every int value, and every intermediate result of an
 *     expression, stays within -32768 to 32767: ccish's int is 16
 *     bits, the host's is wider, and a wraparound would differ. A
 *     test that deliberately exercises that wraparound computes the
 *     exact value on the host, where it must still fit, and passes
 *     every printed or stored result through w16(), the signed
 *     16-bit interpretation of its low 16 bits; ccish wraps at run
 *     time and must agree.
 *   - char is unsigned in both: compile the host build with
 *     -funsigned-char.
 *   - No sizeof, except portable ratios whose answer does not depend
 *     on element size: sizeof a / sizeof a[0] (the element count), or
 *     a type over itself. (sizeof(int) is 2 in ccish, wider on the
 *     host, and a pointer is 2 bytes against 4 or 8.)
 *   - No printing of pointer values, and no dependence on struct
 *     layout: ccish lays out members in order with no padding, the
 *     host may pad. A pointer difference, in elements, is fine.
 *   - No reading uninitialized storage, no undefined behavior; the
 *     host build is checked with -Wall -Wextra.
 *   - puts takes only pstr's result: PLASMA strings are
 *     length-prefixed (pstr points at the length byte), and a C char
 *     array has no such prefix, so it is not a string. Build a
 *     runtime string's bytes and print it with putc.
 *   - Printed bytes are ASCII (0 to 127). plvm's \n and \r come out
 *     as CRLF and the comparison ignores \r, so print \n.
 *
 * Build:
 *   ccish -I <ptest.h's directory> -o p p.c
 *   gcc -Wall -Wextra -funsigned-char -O2 -I <ptest.h's directory> -o p p.c
 * (clang takes the same flags.) The host flags deliberately pin no
 * language standard: the contract's C89 is enforced by ccish's parser,
 * which is where a violation should surface, not by a host flag.
 * The shim's parameters are const char *, so string literals pass
 * cleanly whatever standard the host defaults to.
 *
 * Under ccish (__CCISH__, predefined by ccpp), puti, putc, putln and
 * puts are the CMDSYS's, which ccish names __cmd_puti, __cmd_putc, __cmd_putln and
 * __cmd_puts (the plain names are left for a C library): these are their
 * declarations, and the #defines map the names a test uses to them.
 * The declarations must state the CMDSYS's exact signatures (ccish
 * checks a declaration against them, error 173): __cmd_putc takes a char,
 * not an int. pstr is the parser's builtin (the address of a string
 * literal), declared here for the same reason. Every other compiler
 * takes the #else branch, which defines them in C, matching the
 * CMDSYS's observable behavior for the contract above.
 */
#ifdef __CCISH__
extern void __cmd_puti(int);
extern void __cmd_putc(char);
extern void __cmd_putln(void);
extern void __cmd_puts(const char *);
extern char *pstr(const char *);
#define puti __cmd_puti
#define putc __cmd_putc
#define putln __cmd_putln
#define puts __cmd_puts
/* w16: the signed 16-bit interpretation of a value's low 16 bits,
 * the conventional sign extension: XOR the sign bit, then subtract
 * it (no comparison with 0x7FFF, no 0x10000, no shift by the
 * width). Under ccish, where every int is already 16 bits, it is a
 * no-op normalization. */
static int w16(int v)
{
  return ((v & 0xFFFF) ^ 0x8000) - 0x8000;
}
#else
/* The host's printf and putchar, declared here rather than taking
 * <stdio.h> (a program should not mix C's I/O and PLASMA's anyway).
 * putc and puts are builtins of gcc and clang with different
 * signatures and meanings - a FILE and a return value, and C's puts
 * adds a newline - so the PLASMA ones are defined under other names
 * and the #defines at the end map the names to them; a direct
 * definition may not clash with the builtins.
 * extern int printf(const char *, ...);
 * extern int putchar(int); */
extern int printf(const char *, ...);
extern int putchar(int);
/* Write one byte the way plvm does: raw, with \r and \n becoming the
 * line break (CRLF under plvm; \n here, and the comparison ignores
 * \r). */
__attribute__((unused)) static void ptest_c(int c)
{
  if (c == '\r' || c == '\n')
    putchar('\n');
  else
    putchar(c & 0xFF);
}
/* A signed 16-bit int in decimal, as plvm's puti. */
__attribute__((unused)) static void puti(int n)
{
  printf("%d", n);
}
__attribute__((unused)) static void putln(void)
{
  putchar('\n');
}
/* The string's bytes, up to its NUL, with no newline added: the
 * contract's strings are NUL-terminated on the host, and plvm's
 * length-prefixed literals are NUL-terminated too. */
__attribute__((unused)) static void ptest_s(const char *s)
{
  while (*s)
    ptest_c(*s++);
}
/* The address of the literal. (In ccish, the parser's builtin.) */
__attribute__((unused)) static char *pstr(const char *s)
{
  return (char *)s;
}
/* w16: the signed 16-bit interpretation of a value's low 16 bits,
 * the conventional sign extension: XOR the sign bit, then subtract
 * it. The host's wider int keeps the exact value, which w16 folds
 * to what ccish's 16-bit wrap would give. */
__attribute__((unused)) static int w16(int v)
{
  return ((v & 0xFFFF) ^ 0x8000) - 0x8000;
}
#define putc(c) ptest_c(c)
#define puts(s) ptest_s(s)
#endif

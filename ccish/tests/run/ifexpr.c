/* #if and #elif (approach.md, "ccpp design", step 6): each test prints
 * 1 when the preprocessor gets it right. 16-bit arithmetic, signed or
 * unsigned as C's conversions say; defined; names left over are 0;
 * && || ?: skip what they don't evaluate; #elif after a taken branch
 * isn't evaluated. Then __LINE__, #line, and __FILE__. */
#define TWO 2
#define F(x) ((x) * 3)
int main()
{
#if 1 + 2 * 3 == 7
  __puti(1);
#else
  __puti(0);
#endif
#if defined(TWO) && defined TWO && !defined(THREE)
  __puti(1);
#else
  __puti(0);
#endif
#if F(TWO) == 6 && UNDEFINED == 0
  __puti(1);
#else
  __puti(0);
#endif
#if -1 < 0
  __puti(1);
#else
  __puti(0);
#endif
#if -1 < 0u
  __puti(0);
#else
  __puti(1);
#endif
#if 0x7FFF + 1 < 0        /* 16 bits: it wraps (a documented difference) */
  __puti(1);
#else
  __puti(0);
#endif
#if 10 / 3 == 3 && 10 % 3 == 1 && -7 / 2 == -3 && -7 % 2 == -1
  __puti(1);
#else
  __puti(0);
#endif
#if (1 ? 2 : 3) == 2 && (0 ? 1 / 0 : 4) == 4 && (0 && 1 / 0) == 0 \
    && (1 || 1 / 0)
  __puti(1);
#else
  __puti(0);
#endif
#if (1 << 4) == 16 && (-16 >> 2) == -4 && (0xFFFFu >> 12) == 15 \
    && 'A' == 65
  __puti(1);
#else
  __puti(0);
#endif
#if (5 & 3) == 1 && (5 | 3) == 7 && (5 ^ 3) == 6 && ~0 == -1
  __puti(1);
#else
  __puti(0);
#endif
#if 0
  __puti(9);
#elif TWO == 3
  __puti(9);
#elif TWO == 2
  __puti(1);
#elif 1 / 0
  __puti(9);
#else
  __puti(9);
#endif
#ifdef TWO
#elif 1 / 0
#endif
  __putc(' ');
  __puti(__LINE__);
  __putc(' ');
#line 100
  __puti(__LINE__);
  __putln();
  __puts(pstr(__FILE__));
  __putln();
  return 0;
}

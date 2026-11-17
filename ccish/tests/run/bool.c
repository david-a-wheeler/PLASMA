// bool.c: C23's bool: the keywords bool and _Bool (1 byte, holding
// only 0 or 1), and the pre-defined macros true and false, 1 and 0
// (C23's <stdbool.h> values; ccpp defines them even without the
// header, and doc/ccish-guide.md calls that a quirk). The macros work
// in #if too, and a redefinition with the same body is no diagnostic.
#include <stdbool.h>

#define true 1       // the pre-defined body again
#define false 0

_Static_assert(sizeof(bool) == 1, "a bool is one byte");
_Static_assert(sizeof(_Bool) == 1, "as is its other spelling");

#if true
#define T 1
#else
#define T 0
#endif

#if false
#define F 1
#else
#define F 0
#endif

#if defined(true) && defined(false)
#define D 1
#else
#define D 0
#endif

int main(void)
{
  bool a = true, b = false;
  _Bool c = 7;
  b = 3;
  a = 0;
  __cmd_puti(T * 100 + F * 10 + D);
  __cmd_putc(' ');
  __cmd_puti(true + false);
  __cmd_putc(' ');
  __cmd_puti(a + b + c);
  __cmd_putln();
  return 0;
}

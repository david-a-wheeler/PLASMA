// <assert.h> (lib/include/assert.h): assert(e) is ((void)0), as the
// standard's is with NDEBUG defined, so it emits no code and doesn't
// evaluate e: bump is never called, and assert(0) doesn't stop main.
// It's included twice: like the standard's, it has no include guard.
// listing-lacks: ; bump
#include <assert.h>
#include <assert.h>

int count;

int bump(void)
{
  count++;
  return 0;
}

int main(void)
{
  assert(bump() == 1);
  assert(0);
  __cmd_puti(count);
  __cmd_putln();
  return 0;
}

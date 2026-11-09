// Clicking the Apple II speaker: reading $C030 toggles it. SPEAKER and
// KEYBOARD are written as C programs for real hardware write them: a
// const pointer to volatile char. volatile keeps an optimizing compiler
// such as gcc from deleting the reads; ccish never deletes them anyway.
// Since each pointer is const with a constant value, reading it gives
// that value at compile time, so *SPEAKER is the same single LAB as
// *(char *)0xC030, with no load of the pointer (approach.md, "M5
// design"). click() has no parameters, locals or temporaries, so, as in
// PLASMA's compiler, it has no frame: no ENTER, and RET instead of
// LEAVE. Its read is kept although its value is unused, since it is
// I/O. ready() reads the keyboard at $C000 (bit 7 is set when a key is
// waiting); its early return is RET plus a pad byte. main() has no
// frame either. On plvm, $C000 and $C030 are plain memory holding 0,
// so ready() is 0.
// listing-has: LAB $C030
// listing-has: LAB $C000
// listing-lacks: ENTER
// listing-lacks: LEAVE
// listing-lacks: CW $C0
// listing-count: 4 RET
volatile char *const SPEAKER = (volatile char *)0xC030;
volatile char *const KEYBOARD = (volatile char *)0xC000;

void click(void)
{
  *SPEAKER;
}

int ready(void)
{
  if (*KEYBOARD > 127)
    return 1;
  return 0;
}

int main()
{
  click();
  click();
  click();
  __puti(ready());
  __putln();
  return 0;
}

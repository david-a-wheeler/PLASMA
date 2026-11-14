// main in the second object; a function defined in one object and
// called from the other (upcase: no longer a CMDSYS name of its own, as
// it was, since only CMDSYS may define a __cmd_ name, which
// tests/errors/cmd_define.c checks); and a function declared with ()
// in one object and defined with its parameters in another.
int count;

// This program's own upper case, of a sort.
int upcase(int c)
{
  count++;
  return c + 1;
}

// Called through a () declaration in b.c.
int bump(int n)
{
  return n + count;
}

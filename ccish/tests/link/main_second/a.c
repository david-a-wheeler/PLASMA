// main in the second object; a program's own definition of a CMDSYS
// name (toupper), used instead of the import; and a function declared
// with () in one object and defined with its parameters in another.
int count;

// This program's own toupper, with CMDSYS's type.
int __toupper(int c)
{
  count++;
  return c + 1;
}

// Called through a () declaration in b.c.
int bump(int n)
{
  return n + count;
}

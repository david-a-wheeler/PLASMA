// expect: undeclared identifier 'U'
/* A typedef in a block is gone after the block. */
int main()
{
  {
    typedef int U;
    U a = 1;
  }
  U b;
  return 0;
}

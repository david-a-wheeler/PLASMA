
// A tag declared in a block isn't visible after the block, so
// struct t afterward is a new, incomplete struct.
int main()
{
  {
    struct t { int a; } x;
    x.a = 1;
  }
  struct t y;
  return 0;
}

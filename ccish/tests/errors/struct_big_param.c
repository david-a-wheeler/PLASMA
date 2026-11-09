
// Only structs of up to 8 bytes are passed by value (approach.md, "M6
// design"); a bigger one is passed by pointer.
struct big { int v[5]; };

int first(struct big b)
{
  return b.v[0];
}

int main()
{
  return 0;
}

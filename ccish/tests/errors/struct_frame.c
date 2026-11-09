
// A local struct must fit in the frame, like a local array.
struct big { char buf[200]; };

int main()
{
  struct big a, b;
  a.buf[0] = 1;
  return 0;
}

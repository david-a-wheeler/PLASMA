// expect: pointer arithmetic on a pointer to an incomplete struct
/* Stepping a pointer needs the size of what it points to. */
struct s *p;

int main()
{
  p++;
  return 0;
}

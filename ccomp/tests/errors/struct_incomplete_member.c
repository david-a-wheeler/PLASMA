
/* A pointer to an incomplete struct is fine, but its members can't be
 * used until the struct is defined. */
struct s *p;

int main()
{
  return p->x;
}

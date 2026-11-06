
/* An incomplete struct has no size yet. */
struct s;

int main()
{
  return sizeof(struct s);
}

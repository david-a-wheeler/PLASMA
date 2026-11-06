
/* A returned struct must be the function's struct type. */
struct pt { int x, y; };
struct sz { int w, h; };

struct pt corner(struct sz s)
{
  return s;
}

int main()
{
  return 0;
}


/* An argument must be the parameter's struct type. */
struct pt { int x, y; };
struct sz { int w, h; };

int len(struct pt p);

int main()
{
  struct sz s;
  return len(s);
}

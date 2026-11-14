// An object whose initializer is all zeros goes in the zero-filled
// space, after every object's initialized data: here the data is d
// (data+0) and e (data+2), and z comes after them (z[1] at data+4).
// Kept in the data, as before, z would be at data+2 and e at data+7.
// listing-has: ; data+2
// listing-has: ; data+4
// listing-lacks: ; data+7
int d = 3;
char z[5] = {0};
char e = 4;

int main(void)
{
  z[1] = 5;
  __cmd_puti(d + z[1] + e);
  __cmd_putln();
  return 0;
}

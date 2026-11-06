
/* Only structs of up to 8 bytes are returned by value. */
struct big { int v[5]; };

struct big make(void);

int main()
{
  return 0;
}

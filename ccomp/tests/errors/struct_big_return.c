// expect: a struct over 8 bytes can't be returned by value: return it through a pointer parameter
/* Only structs of up to 8 bytes are returned by value. */
struct big { int v[5]; };

struct big make(void);

int main()
{
    return 0;
}

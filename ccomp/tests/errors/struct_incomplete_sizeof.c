// expect: sizeof can't be applied to the incomplete type 'struct s'
/* An incomplete struct has no size yet. */
struct s;

int main()
{
    return sizeof(struct s);
}

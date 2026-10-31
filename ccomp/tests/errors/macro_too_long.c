// expect: :3: fatal 256: macro expansion too long
#define A(x) x x x x x x x x
int y = A(A(A(A(1))));

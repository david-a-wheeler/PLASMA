#define F(a, b) a + b * 2
#define B(a) [a]
#define S(x) #x
#define CAT(a, b) a ## b
#define V(...) f(__VA_ARGS__)
#define G F
F(1, x)
B(B(1))
S(a  +  "b\n")
CAT(x, 1) CAT(+, =) CAT(, y)
V(1, 2) V()
G(3, 4) F ;

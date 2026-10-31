// expect: :4: error 250: a directive inside a macro's arguments
#define F(x) x
int a = F(1,
#define Y

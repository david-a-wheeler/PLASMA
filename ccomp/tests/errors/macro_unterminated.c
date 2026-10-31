// expect: :4: error 249: unterminated call of macro F
#define F(x) x
int a = F(1;

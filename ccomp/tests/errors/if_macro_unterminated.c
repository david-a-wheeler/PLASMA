// expect: :3: error 249: unterminated call of macro F
#define F(x) x
#if F(1
#endif

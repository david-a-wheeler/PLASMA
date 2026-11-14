struct pt { int x, y; } s, t;
int a;
int main(void) { (a = 1, s).y = 2; return 0; }

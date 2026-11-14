struct pt { int x, y; } s, t;
int a;
int main(void) { (a = 1, s) = t; return 0; }

// expect: :2: error 6: \x used with no following hex digits
int main() { puts(pstr("\xg")); return 0; }

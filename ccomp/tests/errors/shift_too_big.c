// expect: :2: error 35: shift count out of range (0 to 15): too big
int main() { int x = 1; return x << 16; }

// expect: :3: error 79: sizeof can't be applied to the incomplete type
extern int t[];
int main() { return sizeof t; }

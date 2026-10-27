// expect: error: declaration doesn't match the CMDSYS function 'puti'
int puti(int a, int b);
int main() { puti(1, 2); return 0; }

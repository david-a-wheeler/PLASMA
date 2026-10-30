// expect: ccish: error 172: undefined reference to 'g'
/* Declared and used, but defined nowhere: the final pass reports it. */
extern int g;
int main() { return g; }

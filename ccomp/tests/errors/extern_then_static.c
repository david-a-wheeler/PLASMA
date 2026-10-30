// expect: :4: error 217: static declaration follows a non-static declaration of 'x'
/* C11 6.2.2p7: undefined, and gcc rejects it. */
extern int x;
static int x;
int main() { return x; }

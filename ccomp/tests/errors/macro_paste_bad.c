// expect: :3: error 254: pasting doesn't give a valid token: +/
#define C(a, b) a ## b
int x = 1 C(+, /) 2;

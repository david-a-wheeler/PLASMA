// Nor is the function a pointer points to: *fp = 0 would change no
// function (assign to fp instead).
int (*fp)(int);
int main() { *fp = 0; return 0; }

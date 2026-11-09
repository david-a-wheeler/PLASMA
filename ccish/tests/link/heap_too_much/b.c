// With a.c's and c.c's, over 65023 bytes of large globals (65535,
// less the 512 that init leaves for frames): ccrel rejects them.
char two[30000];
int main() { return 0; }

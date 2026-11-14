// A CMDSYS name declared differently, and defined nowhere.
int __cmd_toupper(char *s);
int main() { return __cmd_toupper("x"); }

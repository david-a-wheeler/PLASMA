/* A global declared in one object is a function in the other. */
extern int g;
int main() { return g; }

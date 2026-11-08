/* Only a function, or a pointer to one, can be called: here an array
 * element that is an int. */
int a[2];
int main() { return a[0](1); }

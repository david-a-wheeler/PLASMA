
int trace(int m[][3]);
int main() {
  int m[3][4];
  // m is a pointer to arrays of 4 ints, not of 3
  return trace(m);
}

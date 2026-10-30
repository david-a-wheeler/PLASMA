// expect: :3: error 27: redeclaration of 'n'
int f(int n) {
  int n;
  return n;
}

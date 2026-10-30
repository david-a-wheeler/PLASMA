// expect: :4: error 70: array subscript is not an integer
int main() {
  int a[3], *p = a;
  return a[p];
}

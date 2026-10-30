// expect: :4: error 40: an array can't be the left operand of assignment
int main() {
  int a[3], b[3];
  a = b;
  return 0;
}

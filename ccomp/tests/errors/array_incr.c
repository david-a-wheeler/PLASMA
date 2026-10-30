// expect: :4: error 40: an array can't be the operand of ++ or --
int main() {
  int a[3];
  a++;
  return 0;
}

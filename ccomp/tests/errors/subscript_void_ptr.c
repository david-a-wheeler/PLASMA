// expect: :4: error 45: pointer arithmetic on a 'void *'
int main() {
  void *v = 0;
  return v[1];
}

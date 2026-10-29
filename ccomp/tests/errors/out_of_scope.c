// expect: :4: error 64: undeclared identifier 'inner'
int main() {
    { int inner; inner = 1; }
    return inner;
}

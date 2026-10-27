// expect: :4: error: undeclared identifier 'inner'
int main() {
    { int inner; inner = 1; }
    return inner;
}

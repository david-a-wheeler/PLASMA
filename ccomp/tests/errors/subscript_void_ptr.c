// expect: :4: error: pointer arithmetic on a 'void *'
int main() {
    void *v = 0;
    return v[1];
}

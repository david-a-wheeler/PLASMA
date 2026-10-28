// expect: :4: error: local array doesn't fit in the 255-byte frame: 'buf'
int main() {
    int i;
    char buf[254];
    return 0;
}

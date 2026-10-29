// expect: :4: error 115: local array doesn't fit in the 255-byte frame (make it static or global): 'buf'
int main() {
    int i;
    char buf[254];
    return 0;
}

// expect: :4: error 165: conflicting declaration of 'later'
int main() { later(); return 0; }

void later() { }

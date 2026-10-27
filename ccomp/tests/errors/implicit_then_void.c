// expect: :4: error: conflicting declaration of 'later'
int main() { later(); return 0; }

void later() { }

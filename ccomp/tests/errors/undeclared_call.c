// expect: :4: error 215: function called without a declaration: 'later'
/* No implicit declarations (approach.md, decision 20): a function must
 * be declared before it is called. */
int main() { later(); return 0; }

void later() { }

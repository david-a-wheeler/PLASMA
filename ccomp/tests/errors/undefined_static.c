// expect: ccish: error 172: undefined reference to 'helper'
/* A static function must be defined in its own file. */
static int helper(int n);
int main() { return helper(1); }

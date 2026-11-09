// Names in the name pool (approach.md, "Sym split design"): the names
// of a function's parameters and locals are released at its end, but
// the name of a function first called inside it (a CMDSYS function,
// declared by its first call) must stay, even when other names were
// added before and after it. Locals of the same name in different functions, and
// variables shadowing globals, must still find the right variable.
int count = 100;
int helper_called_early(int n);
int later_too(int z);

int first(int first_param)
{
  int local_only_in_first = first_param * 2;
  int count = 5;                  // hides the global
  local_only_in_first += helper_called_early(__sext(count));
  return local_only_in_first + count;
}

int second(int x)
{
  int another_local_name = x + 1;
  int y = later_too(__toupper(another_local_name));
  return y + count;               // the global
}

int helper_called_early(int n)
{
  int temp_name = n * 10;
  return temp_name;
}

int later_too(int z)
{
  return z * 3;
}

int main()
{
  int local_only_in_first = 7;    // the same name, a new variable
  __puti(first(4)); __putc(' ');
  __puti(second(1)); __putc(' ');
  __puti(__sext(local_only_in_first + count));
  __putln();
  return 0;
}

// Two objects that both use the helper udiv (unsigned / by a value
// known only at run time): ccgen writes it into each, and cclink keeps
// one copy, which both calls use (approach.md, "unsigned and helper
// functions design").
// listing-count: 1 helper udiv:
// listing-count: 2 ; helper udiv
unsigned div_a(unsigned n, unsigned d)
{
  return n / d;
}

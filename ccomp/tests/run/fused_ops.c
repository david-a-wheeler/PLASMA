/* The VM's fused ops (peep.c): a constant added or subtracted is one
 * ADDI or SUBI (or INCR / DECR for 1), and a variable's value added, or
 * added twice (a word index), is one ADDLW / IDXLW, or ADDAW / IDXAW
 * for a global. */
// listing-has: ADDI 100
// listing-has: SUBI 100
// listing-has: SUBI 7
// listing-has: ADDI 9
// listing-has: ADDLW
// listing-has: ADDAW
// listing-has: IDXAW
// listing-lacks: CB 100
// listing-lacks: ADD$
int g = 5;
int k = 1;
int words[4] = {10, 20, 30, 40};

/* n + 100: CB 100 ADD becomes ADDI 100. */
int plus100(int n) { return n + 100; }

/* n - 100: SUBI 100. */
int minus100(int n) { return n - 100; }

/* n + -7: a negative constant added is SUBI 7. */
int plus_minus7(int n) { return n + -7; }

/* n - -9: a negative constant subtracted is ADDI 9. */
int minus_minus9(int n) { return n - -9; }

/* n - -1: INCR. */
int minus_minus1(int n) { return n - -1; }

/* a + b: LLW a LLW b ADD becomes LLW a ADDLW b. */
int sum(int a, int b) { return a + b; }

/* n + g: the global's value added is ADDAW. */
int plus_g(int n) { return n + g; }

/* p[k] for a global k (1, so words[1]): IDXAW. */
int word_at_k(void)
{
  int *p = words;
  return p[k];
}

int main()
{
  puti(plus100(1)); putc(' ');
  puti(minus100(1)); putc(' ');
  puti(plus_minus7(10)); putc(' ');
  puti(minus_minus9(10)); putc(' ');
  puti(minus_minus1(10)); putc(' ');
  puti(sum(3, 4)); putc(' ');
  puti(plus_g(2)); putc(' ');
  puti(word_at_k());
  putln();
  return 0;
}

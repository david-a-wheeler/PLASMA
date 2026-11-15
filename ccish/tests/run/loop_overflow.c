// A loop header that doesn't fit the hold buffer (src/irloop.c,
// LOOP_BUF): loop_overflow() lowers it in place, the test at the top,
// entered by falling in, with the break, step and body jumps kept, so
// each loop has three BRNCH beside its test's branch, where a
// fitting loop's (loop_code.c) at most one is its entry jump. The
// overflow comes in the step (f, a 144-op header) and in the test
// (g, 146 ops), so loop_overflow with and without a held step both
// run. The outputs pin the iteration
// counts: f's step runs three times (k1 + k20 is 6), g's body runs
// twice when the && chain is true (s + i is 16) and not at all when
// it is false (0). f's k's are initialized, so the result doesn't
// depend on the frame's contents before the call (the XLOAD image's
// heap is not zeroed).
// f: its three BRNCH (to the break, test and step) and its test's
// BRTRU; g: its three BRNCH, 70 BRFLS (the i < 2 side and v[0] to
// v[68]) and its test's BRTRU (the last side, v[69], fused with the
// jump to the body).
// listing-count: 6 BRNCH
// listing-count: 2 BRTRU
// listing-count: 70 BRFLS
int v[70];

int f(int i)
{
  int k1 = 0, k2 = 0, k3 = 0, k4 = 0, k5 = 0, k6 = 0, k7 = 0, k8 = 0,
      k9 = 0, k10 = 0, k11 = 0, k12 = 0, k13 = 0, k14 = 0, k15 = 0,
      k16 = 0, k17 = 0, k18 = 0, k19 = 0, k20 = 0;
  for (i = 0; i < 3; k1++, k2++, k3++, k4++, k5++, k6++, k7++, k8++,
                 k9++, k10++, k11++, k12++, k13++, k14++, k15++,
                 k16++, k17++, k18++, k19++, k20++)
    i++;
  return k1 + k20;
}

int g(int i)
{
  int s = 0;
  for (i = 0; i < 2 && v[0] && v[1] && v[2] && v[3] && v[4] && v[5] && v[6] && v[7]
                 && v[8] && v[9] && v[10] && v[11] && v[12] && v[13] && v[14] && v[15]
                 && v[16] && v[17] && v[18] && v[19] && v[20] && v[21] && v[22] && v[23]
                 && v[24] && v[25] && v[26] && v[27] && v[28] && v[29] && v[30] && v[31]
                 && v[32] && v[33] && v[34] && v[35] && v[36] && v[37] && v[38] && v[39]
                 && v[40] && v[41] && v[42] && v[43] && v[44] && v[45] && v[46] && v[47]
                 && v[48] && v[49] && v[50] && v[51] && v[52] && v[53] && v[54] && v[55]
                 && v[56] && v[57] && v[58] && v[59] && v[60] && v[61] && v[62] && v[63]
                 && v[64] && v[65] && v[66] && v[67] && v[68] && v[69]; i++)
    s += 7;
  return s + i;
}

int main(void)
{
  int i;
  for (i = 0; i < 70; i++)
    v[i] = 1;
  __cmd_puti(f(0));
  __cmd_putc(' ');
  __cmd_puti(g(0));
  __cmd_putc(' ');
  v[0] = 0;
  __cmd_puti(g(0));
  __cmd_putln();
  return 0;
}

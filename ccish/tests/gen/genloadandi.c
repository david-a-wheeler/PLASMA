// genloadandi.c - a byte load (LLB, LAB) already zero-extends, so a
// full-byte ANDI 255 right after it is redundant and goes. A word load
// (LLW) keeps its ANDI 255 (the high byte may be set), a byte load under
// a real mask keeps the mask, and c & 255 & 240 merges to the mask.
char g;
int loc(char c){ return c & 255; }        // LLB 0 (folded)
int glo(void){ return g & 255; }          // LAB (folded)
int plain(char c){ return c; }            // LLB 0 (control)
int notbyte(int x){ return x & 255; }     // LLW 0 ANDI 255 (control)
int masked(char c){ return c & 240; }     // LLB 0 ANDI 240 (control)
int both(char c){ return c & 255 & 240; } // LLB 0 ANDI 240 (folded)

int main(void)
{
  loc('a');
  glo();
  plain('a');
  notbyte(300);
  masked('a');
  both('a');
  return 0;
}

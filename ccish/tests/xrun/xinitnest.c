// xinitnest.c: nested braces in initializers (C11 6.7.9). Arrays of
// structs, 2-D arrays, structs holding arrays, structs and unions,
// short inner lists (the rest is zero), strings in braces, an outer
// array of unknown size, empty braces, and a table of structs of
// function pointers; for globals, static locals and locals. Every
// inner list is braced, since gcc's -Wall warns about brace elision;
// tests/run/initnest.c mixes the two.
#include "ptest.h"

struct pt
{
  int x;
  int y;
};

struct box
{
  struct pt lo;
  int v[3];
  char tag[4];
  struct pt hi;
};

union num
{
  struct pt p;
  int i;
};

struct holder
{
  char c;
  union num n;
  int z;
};

struct cmd
{
  char name[4];
  int (*run)(int);
  int arg;
};

int twice(int a)
{
  return 2 * a;
}

int neg(int a)
{
  return -a;
}

struct pt ps[3] = {{1, 2}, {3}, {5, 6}};
int m[3][3] = {{1, 2, 3}, {4}, {7, 8}};
struct pt pts[] = {{10, 11}, {12, 13}, {14, 15}, {16}};
struct box b = {{1, 2}, {3, 4}, "ab", {5, 6}};
struct box bs[2] = {{{1}, {2}, {'x'}, {3}}, {{4, 5}, {6, 7, 8}, "yz", {9, 10}}};
struct holder h = {'q', {{20, 21}}, 22};
char names[3][4] = {"ab", {'c', 'd'}, "e"};
char word[4] = {"hi"};
struct cmd cmds[] = {{"dbl", twice, 4}, {"neg", neg, 5}, {"nul", 0, 0}};
int empty[2][2] = {{1, 2}, {}};

// Print a struct pt as x,y.
void showpt(struct pt p)
{
  puti(p.x);
  putc(',');
  puti(p.y);
  putc(' ');
}

// Print the chars of a 4-char array, a 0 byte as '.'.
void show4(char *s)
{
  int i;
  for (i = 0; i < 4; i++)
    putc(s[i] ? s[i] : '.');
  putc(' ');
}

// Print a struct box.
void showbox(struct box *p)
{
  int i;
  showpt(p->lo);
  for (i = 0; i < 3; i++) {
    puti(p->v[i]);
    putc(' ');
  }
  show4(p->tag);
  showpt(p->hi);
  putln();
}

// A static local with nested braces, read twice.
int statk(int k)
{
  static int t[2][2] = {{1, 2}, {3}};
  return t[k / 2][k % 2];
}

int main(void)
{
  struct pt lps[3] = {{-1, -2}, {-3}, {-5, -6}};
  int lm[2][3] = {{9}, {8, 7}};
  char lc[2][3] = {{'a'}, {'b', 'c'}};
  struct box lb = {{1, 2}, {3}, "c", {4}};
  struct cmd lcmd[2] = {{"two", twice, 6}, {{'n', 'g'}, neg, 7}};
  int i, j;

  for (i = 0; i < 3; i++)
    showpt(ps[i]);
  putln();
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++) {
      puti(m[i][j]);
      putc(' ');
    }
  putln();
  puti((int)(sizeof pts / sizeof pts[0])); putc(' ');
  for (i = 0; i < 4; i++)
    showpt(pts[i]);
  putln();
  showbox(&b);
  showbox(&bs[0]);
  showbox(&bs[1]);
  putc(h.c); putc(' ');
  showpt(h.n.p);
  puti(h.z); putln();
  for (i = 0; i < 3; i++)
    show4(names[i]);
  show4(word);
  putln();
  for (i = 0; cmds[i].run; i++) {
    show4(cmds[i].name);
    puti(cmds[i].run(cmds[i].arg)); putc(' ');
  }
  puti(cmds[2].arg); putln();
  for (i = 0; i < 4; i++) {
    puti(empty[i / 2][i % 2]); putc(' ');
    puti(statk(i)); putc(' ');
  }
  putln();

  for (i = 0; i < 3; i++)
    showpt(lps[i]);
  for (i = 0; i < 6; i++) {
    puti(lm[i / 3][i % 3]); putc(' ');
  }
  for (i = 0; i < 6; i++)
    putc(lc[i / 3][i % 3] ? lc[i / 3][i % 3] : '.');
  putln();
  showbox(&lb);
  for (i = 0; i < 2; i++) {
    show4(lcmd[i].name);
    puti(lcmd[i].run(lcmd[i].arg)); putc(' ');
  }
  putln();
  return 0;
}

// xlist.c — loops through non-integers: a chain of struct nodes,
// whose array positions are scrambled (neither the logical order
// nor its reverse), walked through the next pointers to the null
// terminator, and also walked in physical order, forwards through
// array dereference and backwards through a pointer to a node,
// beside counted for loops that go up and go down, each in its own
// function, against bounds passed in, not constants, chosen so that
// every answer line is different, so no two lines can stand in for
// each other. The accumulator is order-sensitive, so a walk in the
// wrong order gives the wrong answer.
#include "ptest.h"

struct node
{
  int value;
  struct node *next;
};

// The node with value v sits at array position (5 * v) % n: a
// permutation of 0 to n-1 (5 is coprime to 7 and 12), neither the
// logical order nor its reverse.
int pos(int v, int n)
{
  return (5 * v) % n;
}

// Set each node's value, point each next at the logically next
// node, and make the final node's next the null pointer. Returns
// the head, the node with value 1.
struct node *build(struct node *nodes, int n)
{
  int v;
  for (v = 1; v <= n; v++)
    nodes[pos(v, n)].value = v;
  for (v = 1; v < n; v++)
    nodes[pos(v, n)].next = &nodes[pos(v + 1, n)];
  nodes[pos(n, n)].next = 0;
  return &nodes[pos(1, n)];
}

// Walk the chain in logical order: accum = 2*accum + p->value.
// The order matters, so the answer pins the order of the walk.
int walk(struct node *head)
{
  struct node *p;
  int accum;
  accum = 0;
  for (p = head; p != 0; p = p->next)
    accum = 2 * accum + p->value;
  return accum;
}

// A for loop going up, bounded by n, a value passed in rather
// than a constant.
int up(int n)
{
  int i, s;
  s = 0;
  for (i = 1; i <= n; i++)
    s += i;
  return s;
}

// Walk the array in physical order (the scrambled order), reading
// each member through the array: nodes[i].value.
int walk_phys(struct node *nodes, int n)
{
  int i;
  int accum;
  accum = 0;
  for (i = 0; i < n; i++)
    accum = 2 * accum + nodes[i].value;
  return accum;
}

// Walk the physical array backwards with a pointer to a node:
// p->value and p-- through the array of structs.
int walk_back(struct node *nodes, int n)
{
  struct node *p;
  int accum;
  accum = 0;
  for (p = &nodes[n - 1]; p >= nodes; p--)
    accum = 2 * accum + p->value;
  return accum;
}

// A for loop going down, bounded by n.
int down(int n)
{
  int i, s;
  s = 0;
  for (i = n; i >= 1; i -= 1)
    s += i;
  return s;
}

int main(void)
{
  struct node big[12];
  struct node small[7];
  puti(up(12));               // 78
  putln();
  puti(down(7));              // 28
  putln();
  puti(walk(build(big, 12))); // 8178
  putln();
  puti(up(9));                // 45
  putln();
  puti(down(4));              // 10
  putln();
  puti(walk(build(small, 7))); // 247
  putln();
  puti(walk_phys(small, 7));  // 682
  putln();
  puti(walk_back(small, 7));  // 421
  putln();
  return 0;
}

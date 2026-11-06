/* genglobal.c - the globals: a load or store of a global is one op in
 * its absolute form (LAW, DAW), and a store followed by a load of the
 * same global keeps the value for the load (the SAW merges with the
 * LAW into a DAW), so "h = g" reuses the value "g = 5" just stored. A
 * statement store keeps no copy of its value. */
int g;
int h;

int f(void)
{
  g = 5;
  h = g;       /* the 5 is still on the stack: no load of g */
  return h;    /* and still on the stack: no load of h */
}

int main(void)
{
  return f() == 5;
}

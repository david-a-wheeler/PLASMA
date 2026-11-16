// sf is static in a.c, so it is not shared: this use of it fails the
// link. sf2 is not static: it is shared, and this use of it links.
int sf(int n);
int sf2(int n);

int use(void)
{
  return sf(2) + sf2(2);
}

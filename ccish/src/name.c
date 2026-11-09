// The table of enum constants and typedef names (approach.md, "enum and
// typedef design"), as parallel arrays: entry i has name id_nm[i] (an
// id, the number ccpp gave the name: ../CCPP.md), value id_v[i], kind
// id_k[i] (ID_*), and block depth id_d[i], one more than the depth
// (0 once hidden, since char is unsigned: the 0-means-none idiom, as
// in irin.c's lab_depth). Names need
// no pool of their own: cccomp keeps no spellings (approach.md, "ccpp
// design").
#include <stddef.h>
#include "build.h"
#include "diag.h"
#include "name.h"
#include "name.tab.h"

static int id_nm[MAX_IDS];
int id_v[MAX_IDS];                  // (name.tab.h)
char id_k[MAX_IDS];
char id_d[MAX_IDS];
static int nids;
#ifdef CCISH_STATS
static int nids_peak;               // the most at once
#endif

// Add an enum constant or typedef name (kind ID_*) with value val,
// visible from block depth depth on, and return it. The caller checks
// that the name is new in its scope.
int id_add(int name, int kind, int val, int depth)
{
  if (nids >= MAX_IDS)
    fatal0(19 /* "too many enum constants and typedef names" */);
  id_nm[nids] = name;
  id_v[nids] = val;
  id_k[nids] = (char)kind;
  id_d[nids] = (char)(depth + 1);
#ifdef CCISH_STATS
  if (nids + 1 > nids_peak)
    nids_peak = nids + 1;
#endif
  return nids++;
}

// The innermost visible enum constant or typedef named name, or -1.
int id_find(int name)
{
  int i;
  for (i = nids - 1; i >= 0; i--)
    if (id_nm[i] == name && id_d[i] != 0)
      return i;
  return -1;
}

// Hide the entries declared deeper than depth, at the end of their
// block, and free them if they are the last ones: unlike struct tags,
// no type refers to them.
void id_hide(int depth)
{
  int i;
  for (i = 0; i < nids; i++)
    if (id_d[i] > depth + 1)
      id_d[i] = 0;
  while (nids > 0 && id_d[nids - 1] == 0)
    nids--;
}

#ifdef CCISH_STATS
// The most entries the table held at once (CCISH_STATS).
int id_peak(void)
{
  return nids_peak;
}
#endif

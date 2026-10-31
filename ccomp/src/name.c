/*
 * The table of enum constants and typedef names (approach.md, "enum and
 * typedef design"), as parallel arrays: entry i has name id_nm[i] (an
 * id, the number ccpp gave the name: ../CCPP.md), value id_v[i], kind
 * id_k[i] (ID_*), and block depth id_d[i] (-1 once hidden). Names need
 * no pool of their own: cccomp keeps no spellings (approach.md, "ccpp
 * design").
 */
#include <stddef.h>
#include "build.h"
#include "diag.h"
#include "name.h"

#define MAX_IDS      203    /* enum constants and typedef names (184, in
                             * parse.c) */

static int id_nm[MAX_IDS];
static int id_v[MAX_IDS];
static char id_k[MAX_IDS];
static char id_d[MAX_IDS];
static int nids;
#ifdef CCISH_STATS
static int nids_peak;               /* the most at once */
#endif

/* Add an enum constant or typedef name (kind ID_*) with value val,
 * visible from block depth depth on, and return it. The caller checks
 * that the name is new in its scope. */
int id_add(int name, int kind, int val, int depth)
{
  if (nids >= MAX_IDS)
    fatal(19 /* "too many enum constants and typedef names" */, NULL);
  id_nm[nids] = name;
  id_v[nids] = val;
  id_k[nids] = (char)kind;
  id_d[nids] = (char)depth;
#ifdef CCISH_STATS
  if (nids + 1 > nids_peak)
    nids_peak = nids + 1;
#endif
  return nids++;
}

/* The innermost visible enum constant or typedef named name, or -1. */
int id_find(int name)
{
  int i;
  for (i = nids - 1; i >= 0; i--)
    if (id_nm[i] == name && id_d[i] >= 0)
      return i;
  return -1;
}

/* The kind of entry i (ID_*). */
int id_kind(int i)
{
  return id_k[i];
}

/* The value of entry i: a constant's value, or a typedef's type. */
int id_val(int i)
{
  return id_v[i];
}

/* The block depth entry i was declared at. */
int id_depth(int i)
{
  return id_d[i];
}

/* Hide the entries declared deeper than depth, at the end of their
 * block, and free them if they are the last ones: unlike struct tags,
 * no type refers to them. */
void id_hide(int depth)
{
  int i;
  for (i = 0; i < nids; i++)
    if (id_d[i] > depth)
      id_d[i] = -1;
  while (nids > 0 && id_d[nids - 1] < 0)
    nids--;
}

#ifdef CCISH_STATS
/* The most entries the table held at once (CCISH_STATS). */
int id_peak(void)
{
  return nids_peak;
}
#endif

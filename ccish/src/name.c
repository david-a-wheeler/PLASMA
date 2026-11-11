// The table of enum constants and typedef names (approach.md, "enum and
// typedef design"), as parallel arrays: entry i has name id_nm[i] (an
// id, the number ccpp gave the name: ../CCPP.md), value id_v[i], kind
// id_k[i] (ID_*), and block depth id_d[i]. An entry goes when its
// block ends (id_hide()), so every entry is visible, and the depths
// never fall from one entry to the next: each is added at the current
// depth, and the deeper ones went when their blocks ended. Names need
// no pool of their own: cccomp keeps no spellings (approach.md, "ccpp
// design").
//
// With CCISH_FAST (build.h), the entries are also on hash chains, so
// that id_find() looks at a few, not all: its scan of every entry is
// about a third of cccomp's VM ops compiling ccish's own sources,
// which have about 200 such names (MAX_IDS's peak), and the chains
// cut cccomp's ops by about 32%. Chain ID_CHAIN(name) starts at entry
// id_head[] - 1 (0: no entry), and each entry leads to the next older
// one on its chain, id_next[] - 1, so the newest is found first. A new
// entry goes first on its chain, and only the newest entries are
// removed (id_hide()), so a removed entry is always first on its
// chain. The chains cost 267 bytes of data and 80 of code, which
// cccomp can't spare yet, so the default is the scan of every entry.
#include <assert.h>
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
#ifdef CCISH_FAST
#define ID_CHAINS    64     // a power of 2, for ID_CHAIN's mask
#define ID_CHAIN(name) ((name) & (ID_CHAINS - 1))
_Static_assert((ID_CHAINS & (ID_CHAINS - 1)) == 0,
               "ID_CHAINS isn't a power of 2");
// An entry plus 1, as the chains hold it: a byte, while MAX_IDS
// allows (measure.py's big build raises it).
#if MAX_IDS < 255
typedef unsigned char IdLink;
#else
typedef int IdLink;
#endif
static IdLink id_head[ID_CHAINS];
static IdLink id_next[MAX_IDS];
#endif
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
  assert(nids == 0 || id_d[nids - 1] <= depth);  // the depths never fall
  id_nm[nids] = name;
  id_v[nids] = val;
  id_k[nids] = (char)kind;
  id_d[nids] = (char)depth;
#ifdef CCISH_FAST
  id_next[nids] = id_head[ID_CHAIN(name)];
  id_head[ID_CHAIN(name)] = (IdLink)(nids + 1);
#endif
#ifdef CCISH_STATS
  if (nids + 1 > nids_peak)
    nids_peak = nids + 1;
#endif
  return nids++;
}

#ifdef CCISH_FAST
// The innermost visible enum constant or typedef named name, or -1:
// the faster version, along name's chain.
int id_find(int name)
{
  int i;
  for (i = id_head[ID_CHAIN(name)]; i != 0; i = id_next[i - 1])
    if (id_nm[i - 1] == name)
      return i - 1;
  return -1;
}
#else
// The innermost visible enum constant or typedef named name, or -1:
// the smaller version, a scan of every entry.
int id_find(int name)
{
  int i;
  for (i = nids - 1; i >= 0; i--)
    if (id_nm[i] == name)
      return i;
  return -1;
}
#endif

// Remove the entries declared deeper than depth, at the end of their
// block (unlike struct tags, no type refers to them): the last ones,
// as the depths never fall.
void id_hide(int depth)
{
  while (nids > 0 && id_d[nids - 1] > depth) {
    nids--;
#ifdef CCISH_FAST
    id_head[ID_CHAIN(id_nm[nids])] = id_next[nids];  // first on its chain
#endif
  }
}

#ifdef CCISH_STATS
// The most entries the table held at once (CCISH_STATS).
int id_peak(void)
{
  return nids_peak;
}
#endif

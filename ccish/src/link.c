// The final pass, a static linker: turn the stream files and symbol
// summaries of one or more compiled files, its objects (../OBJECT.md),
// into one PLASMA REL module (../../doc/Bytecodes.md), and print the
// listing (-l), since only this pass knows the final addresses.
//
// It uses none of the compiler's tables (fn_name[] and so on), only the
// files, so it is a program of its own, ccrel (approach.md, "B3").
// The module exports nothing and imports only CMDSYS functions, by their
// PLASMA names (__puts imports PUTS), so other C names never meet
// PLASMA's symbol table (approach.md issue 11, decision 35).
//
// The files are read in order, with no seeking, and only one at a time
// (approach.md 18.6 B6a):
//   1. every object's symbol summary, to lay out the module and to
//      build one table of the names shared between objects (g_name[]...),
//      where duplicates, conflicting types and undefined names are
//      found;
//   2. the header and dependencies are written; then each object's
//      stream is read for its data records, with the data relocations
//      applied, and each again for its strings;
//   3. each stream is read a third time for its functions, each copied
//      with its relocations applied; then the init function is added;
//   4. the RLD and ESD are written. The relocations aren't kept in
//      memory (approach.md 18.6 B4): each summary is read again for
//      the function entries, which the loaders want first, and then
//      each stream a fourth time for the relocations.
// In steps 2 to 4, each object's summary is read again just before its
// stream, to map its own symbol numbers to final addresses, so only
// one object's map is in memory.
//
// With -x (opt_xload), it writes an XLOAD image instead
// (../doc/xload.md, section 3): the same body, for load address
// 0; a table of its functions, in address order, by whose numbers
// every reference to a function is written, so that XLOAD can lay out
// the def entries each VM wants; and compact fixups in address order,
// for which step 4 reads the streams twice, for the data's and then
// the code's.
//
// A global defined with no data bytes (SYM_BSS: zero-filled, at least
// BSS_MIN bytes) is placed in step 1 (approach.md, "Large zero-filled
// data on the heap"). If no object's data holds its address
// (SYM_INDATA), it moves to the heap: it gets a pointer word in the
// data, each LA of it becomes an LAW of that word, and the init
// function allocates every moved object in one block, clears it, and
// fills in the pointer words, before it calls main. Otherwise it stays
// in the module, as zeros after every object's data.
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.opt.h"
#include "bio.h"
#include "obj.h"
#include "obj.fmt.h"
#include "obj.sym.h"
#include "mop.h"
#include "link.h"

#define MAX_GSYMS   307     // limit link: names shared between objects: the
                            // first prime about 10% above what linking
                            // cccomp, with the library, needs
#define MAX_GNAMES  3000    // limit link: bytes of those names, with their
                            // NULs: about 10% above what linking cccomp needs
#define MAX_SEL_TABLES 32   // SEL case tables the listing of one
                            // function waits for at once
#define MAX_MOVED   32      // limit link: large globals moved to the heap in
                            // one module (OBJECT.md, "Zero-filled globals")
#define MAX_DEFS    320     // limit link: functions in an XLOAD image's
                            // table (-x), helpers and init included: about
                            // 10% above what cccomp needs

// A symbol summary entry of the object being read: a function's, or
// a global variable's (which has a size and no parameter count).
typedef struct {
  char name[MAX_NAME + 1];
  int flags;          // SYM_*
  int nparams;        // functions: 255 if unknown
  int size;           // data: bytes, 0 if unknown
  int off;            // if defined: offset in the object's code or data
  int hash;           // type hash, 0 if unknown
  int mod;            // SYM_FIMPORT, SYM_DIMPORT: its module, 1 plus an
                      // obj_mod[] index, or 0 for none
  char label[MAX_NAME + 1];   // and its PLASMA name
} Lsym;

// The modules to import from (approach.md issue 22): each one's name,
// as the objects' summaries give it, and whether a used import comes
// from it, so that it goes in the module's dependencies; and, for the
// object being read, each of its modules' index here.
#define MAX_MODS 8          // limit link: modules one link imports from
static char mod_name[MAX_MODS][MAX_NAME + 1];
static char mod_used[MAX_MODS];
static int nmods;
static unsigned char obj_mod[MAX_MODS];

// How many symbols of each kind the object being read has.
static int nfsyms, ndsyms;

// The object being read (an index in obj_name[]), and its sizes.
static int cur;
static int data_size, strs_size, code_size, bss_size;

// The zero-filled globals (SYM_BSS): those moved to the heap, each with
// a pointer word, by its number (mv_size[]: its bytes), and the bytes of
// those that stay, after every object's data. obj_mv[k] and obj_zs[k]
// are where object k's own (static) ones start, in each.
static int mv_size[MAX_MOVED];
static int nmoved;
static unsigned mv_total;            // and all their bytes
#define HEAP_SPARE 512               // heap init leaves for main's frames
static int zstay;
static int obj_mv[MAX_OBJS + 1], obj_zs[MAX_OBJS + 1];
static int patch_law;               // add_reloc() met a moved global

// The objects, by output name as given to cccomp, and where each one's
// data, strings and code start in the module's (as offsets from the
// start of each). Entry nobjs holds the totals.
static const char *obj_name[MAX_OBJS];
static int nobjs;
static int obj_data[MAX_OBJS + 1], obj_strs[MAX_OBJS + 1];
static int obj_code[MAX_OBJS + 1];

// The names shared between objects (non-local functions and globals),
// as parallel arrays, MAX_GSYMS slots each, with open addressing: a name
// is in the slot gsym_start() picks or a later one, wrapping around
// (approach.md issue 11, "Fitting the linker's tables"). Its description
// is the most complete one met so far, from object g_desc[].
static int g_name[MAX_GSYMS];   // offset in gnames[], or -1: slot empty
static int g_hash[MAX_GSYMS];   // type hash, 0 if unknown
static int g_count[MAX_GSYMS];  // a function's parameter count (255 if
                                // unknown), or a global's size (0)
static int g_off[MAX_GSYMS];    // if defined: offset in its object's
                                // code or data; if imported: ESD index
static unsigned char g_def[MAX_GSYMS];  // defining object, or NO_OBJ
static unsigned char g_desc[MAX_GSYMS]; // object of the description
static unsigned char g_flags[MAX_GSYMS];    // G_*
static int g_lab[MAX_GSYMS];    // may be a PLASMA import: its PLASMA name,
                                // an offset in gnames[]; or -1
static unsigned char g_mod[MAX_GSYMS];  // its module, 1 plus a mod_name[]
                                        // index, or 0 for none

#define NO_OBJ 255

// The helper functions (approach.md, "unsigned and helper functions
// design"), as parallel arrays by helper number: the first object that
// holds each (NO_OBJ if none), its size and checksum, which every other
// object's copy must match, and its offset in the module's code (after
// every object's code). Only the first copy is kept.
static unsigned char h_def[NHELPERS];
static int h_size[NHELPERS];
static int h_sum[NHELPERS];
static int h_off[NHELPERS];
static int nhelpers;            // helpers in the module
#ifdef CCISH_LISTING
#define MAX_HNAME 8
static char h_name[NHELPERS][MAX_HNAME + 1];   // for the listing
#endif
// The CMDSYS comparisons, numbered after the helpers (obj.fmt.h).
static const char *const h_cmdsys[NHELP_CMDSYS] = { HELP_CMDSYS_NAMES };
static void use_cmdsys(const char *name);

// A name's flags. The first three are what its entries say, over all
// the objects: it is used; some entry isn't a CMDSYS function; some is a
// CMDSYS name declared differently. Then: it is a global variable, not a
// function; the function returns a value; it is imported; it is
// zero-filled (SYM_BSS), and after step 1 its g_off is its place: a
// moved global's number, or its offset after every object's data;
// some object's data holds its address (so it stays).
enum { G_USED = 1, G_PLAIN = 2, G_BADCMDSYS = 4, G_DATA = 8,
       G_RETURNS = 16, G_IMPORT = 32, G_BSS = 64, G_INDATA = 128 };

static int ngsyms;          // slots in use
static char gnames[MAX_GNAMES];
static int gnames_len;
static int main_sym = -1;   // the slot of main, or -1
static int nimports, ndefined;

// The object being read in steps 2 and 3, mapped: each function symbol
// to its offset in the module's code, or -2 - its import index, or -1
// if it is neither; and each data symbol to its offset in the module's
// data, or -2 - its number if it moved to the heap, or DM_IMPORT - its
// import index if it is imported, or -1.
#define DM_IMPORT (-0x2000)
#define IS_DIMPORT(v) ((v) <= DM_IMPORT)
static int fmap[MAX_FUNCS];
static int dmap[MAX_VARS];

#ifdef CCISH_LISTING
// For the listing (-l), each function symbol's name as an offset in
// gnames[], or -1 if it has none there. A shared name is already in
// the table; a static function's name is added after the shared names,
// from lnames_len on, until the next object's replace it. One that
// doesn't fit is listed by its address (approach.md issue 11, "Item 1:
// the linker's local names").
static int fname[MAX_FUNCS];
static int lnames_len;
#endif

// Load addresses: of the module, of the data segment, the string pool
// after it, and the code segment after that.
#define MODULE_BASE 0x1000
static int data_base, strs_base, subseg;

int opt_xload;              // (link.h)

// With -x: each function's code-segment offset, in address order, as
// the image's function table holds them; and the fixups' cursor, the
// body offset just past the last word they patched.
static unsigned def_off[MAX_DEFS];
static int ndefs;
static unsigned fx_at;

#ifdef CCISH_LISTING
// The relocations of the function being listed (-l), as parallel
// arrays: each word's load address; its kind, REL_DATA, REL_STR or
// REL_FUNC (a REL_DSYM is kept as the REL_DATA it resolves to); and
// its target, a data or string pool offset in the module, or a function
// symbol of the current object (-1: main). The RLD's relocations are
// not kept: step 4 reads them again (approach.md 18.6 B4).
static int lr_at[MAX_FUNC_RELOCS];
static char lr_kind[MAX_FUNC_RELOCS];
static int lr_target[MAX_FUNC_RELOCS];
static int nlr;
#endif

static unsigned char fbuf[MAX_FUNC_CODE];

// Stop with an error: stream file what is cut short or disagrees with
// the others.
static void damaged(const char *what)
{
  fatal_d(188 /* "compiler stream file is damaged:" */, what);
}

// Stop with an error: the current object's stream file is damaged.
static void bad_obj(void)
{
  damaged(stream_name(obj_name[cur], SUFFIX_OBJ));
}

// ---------- Reading the objects (bio.c's, for ccrel only) ----------

// A little-endian word. Running out of data here means the file is
// damaged, which only the final pass can see.
static int bgetw(Bfile *b)
{
  int lo = bgetc(b), hi = bgetc(b);
  if (lo == EOF || hi == EOF)
    fatal0(3 /* "unexpected end of a compiler stream file" */);
  return lo | (hi << 8);
}

// Read past the next n bytes, which must be there: the rest of the
// buffer at a time, not a byte at a time.
static void bskip(Bfile *b, int n)
{
  int k;
  while (n > 0) {
    if (b->pos >= b->len) {
      bfill(b);
      if (b->len == 0)
        fatal0(3 /* "unexpected end of a compiler stream file" */);
    }
    k = b->len - b->pos;
    if (k > n)
      k = n;
    b->pos += k;
    n -= k;
  }
}

// ---------- Step 1: the symbol summaries ----------

// The file the final pass is reading (in) and the module it writes
// (out). They are static, not locals, so that their buffers
// take no frame space (a frame holds 255 bytes), and so that
// link_abort() can close them after an error.
static Bfile in, out;

// After an error: close whichever of the final pass's files are open
// (ProDOS needs it).
void link_abort(void)
{
  bclose(&in);
  bclose(&out);
}

// Read a symbol's name from the summary (file name) into buf, which
// holds MAX_NAME + 1 characters.
static void read_name(char *buf, const char *name)
{
  int n = bgetc(&in), j;
  if (n < 1 || n > MAX_NAME)
    damaged(name);
  for (j = 0; j < n; j++)
    buf[j] = (char)bgetc(&in);
  buf[n] = 0;
}

// The number of modules the object being read names.
static int nobj_mods;

// Read the modules the object's summary (file name name) names, and map
// each to its index in mod_name[] (obj_mod[]), adding it if it is new.
static void read_mods(const char *name)
{
  char buf[MAX_NAME + 1];
  int n = bgetc(&in), i, m;
  if (n == EOF || n > MAX_MODS)
    damaged(name);
  nobj_mods = n;
  for (i = 0; i < n; i++) {
    read_name(buf, name);
    for (m = 0; m < nmods && strcmp(mod_name[m], buf); m++)
      ;
    if (m == nmods) {
      if (nmods >= MAX_MODS)
        fatal0(224 /* "too many modules imported" */);
      strcpy(mod_name[nmods++], buf);
    }
    obj_mod[i] = (unsigned char)m;
  }
}

// Report error msg about the symbol named name, quoted.
static void undefined(int msg, const char *name)
{
  char buf[MAX_NAME + 3];
  buf[0] = '\'';
  strcpy(buf + 1, name);
  strcat(buf, "'");
  error_d(msg, buf);
}

// Report error msg about the shared name name, as declared or defined
// in objects a and b: 'name' (in a and b), with each object's name cut
// to 80 characters.
static void between(int msg, const char *name, int a, int b)
{
  static char buf[MAX_NAME + 180];
  strcpy(buf, "'");
  strcat(buf, name);
  strcat(buf, "' (in ");
  strncat(buf, obj_name[a], 80);
  strcat(buf, " and ");
  strncat(buf, obj_name[b], 80);
  strcat(buf, ")");
  error_d(msg, buf);
}

// Open object k's symbol summary, check its header, and read its sizes
// and symbol counts; k becomes the current object. Returns the file
// name, for messages.
static const char *open_syms(int k)
{
  const char *name = stream_name(obj_name[k], SUFFIX_SYMS);
  cur = k;
  if (!bopen(&in, name, 0))
    fatal_d(171 /* "cannot open" */, name);
  if (bgetc(&in) != 'C' || bgetc(&in) != 'I' || bgetc(&in) != OBJ_VERSION)
    damaged(name);
  data_size = bgetw(&in);
  strs_size = bgetw(&in);
  code_size = bgetw(&in);
  bss_size = bgetw(&in);
  nfsyms = bgetw(&in);
  ndsyms = bgetw(&in);
  if (nfsyms > MAX_FUNCS)
    fatal0(26 /* "too many functions" */);
  if (ndsyms > MAX_VARS)
    fatal0(28 /* "too many variables" */);
  read_mods(name);
  return name;
}

// Read the next symbol summary entry into s: a data symbol's if data,
// else a function symbol's. name is the summary's file name.
static void read_sym(Lsym *s, int data, const char *name)
{
  int n;
  s->flags = bgetc(&in);
  if (data) {
    s->off = bgetw(&in);
    s->size = bgetw(&in);
    s->nparams = 255;
  } else {
    s->nparams = bgetc(&in);
    s->off = bgetw(&in);
    s->size = 0;
  }
  s->hash = bgetw(&in);
  read_name(s->name, name);
  s->mod = 0;
  s->label[0] = 0;
  if (s->flags & (data ? SYM_DIMPORT : SYM_FIMPORT)) {
    if ((n = bgetc(&in)) == EOF || n > nobj_mods)
      damaged(name);
    s->mod = n;
    read_name(s->label, name);
  }
  // A function has at least one byte of code, so it can't start at
  // the code's end.
  if ((s->flags & SYM_DEFINED)
      && (!data ? s->off >= code_size
          : s->flags & SYM_BSS ? s->size < BSS_MIN
                                 || s->off > bss_size - s->size
          : s->off > data_size))
    damaged(name);
}

// Read the list of helpers at the summary's end (file name): each
// one's number, size, checksum and name. If keep is set (step 1), note
// the first object to hold each, and check that the others' copies
// agree; a CMDSYS comparison (numbered after the helpers, with no
// code) is imported instead. Then check that nothing follows, and
// close the summary.
static void close_syms(const char *name, int keep)
{
  int n = bgetc(&in), k, size, sum, len, j;
  char buf[MAX_NAME + 1];
  if (n == EOF || n > NHELPERS + NHELP_CMDSYS)
    damaged(name);
  while (n-- > 0) {
    k = bgetc(&in);
    size = bgetw(&in);
    sum = bgetw(&in);
    len = bgetc(&in);
    if (k < 0 || k >= NHELPERS + NHELP_CMDSYS || len < 1 || len > MAX_NAME)
      damaged(name);
    for (j = 0; j < len; j++)
      buf[j] = (char)bgetc(&in);
    buf[len] = 0;
    if (!keep)
      continue;
    if (k >= NHELPERS) {
      use_cmdsys(h_cmdsys[k - NHELPERS]);
      continue;
    }
    if (h_def[k] == NO_OBJ) {
      h_def[k] = (unsigned char)cur;
      h_size[k] = size;
      h_sum[k] = sum;
      nhelpers++;
#ifdef CCISH_LISTING
      if (len > MAX_HNAME)
        len = MAX_HNAME;        // cut, for the listing only
      memcpy(h_name[k], buf, len);
      h_name[k][len] = 0;
#endif
    } else if (h_size[k] != size || h_sum[k] != sum) {
      error_d(229 /* "helper functions differ between objects (compiled "
                     "by different ccgens?):" */, buf);
    }
  }
  if (bgetc(&in) != EOF)
    damaged(name);
  bclose(&in);
}

// The slot where the search for name starts; name may be in a later
// one. The hash is h = (h + h) ^ c, cheap in the VM (approach.md issue
// 11, "Fitting the linker's tables"); masking each step keeps a 32-bit
// host from overflowing, and gives the same slots.
static int gsym_start(const char *name)
{
  int h = 0;
  while (*name)
    h = ((h + h) ^ *name++) & 0x7FFF;
  return h % MAX_GSYMS;
}

// The slot actually holding name, or -1 if it isn't in the table.
static int gsym_find(const char *name)
{
  int g = gsym_start(name), n;
  for (n = 0; n < MAX_GSYMS && g_name[g] >= 0; n++) {
    if (!strcmp(gnames + g_name[g], name))
      return g;
    if (++g == MAX_GSYMS)
      g = 0;
  }
  return -1;
}

// Add name to gnames[], and return its offset there. Stops at once if
// it is full.
static int add_gname(const char *name)
{
  int n = (int)strlen(name) + 1, at = gnames_len;
  if (gnames_len + n > MAX_GNAMES)
    fatal0(222 /* "too many names shared between objects" */);
  memcpy(gnames + at, name, n);
  gnames_len += n;
  return at;
}

// Put name, which isn't in the table, in the first empty slot from its
// starting slot, with no description or definition yet; returns the
// slot. Stops at once if the table or its names are full.
static int gsym_add(const char *name)
{
  int g;
  if (ngsyms >= MAX_GSYMS)
    fatal0(222 /* "too many names shared between objects" */);
  for (g = gsym_start(name); g_name[g] >= 0; )
    if (++g == MAX_GSYMS)
      g = 0;
  ngsyms++;
  g_name[g] = add_gname(name);
  g_lab[g] = -1;
  g_mod[g] = 0;
  g_flags[g] = 0;
  g_def[g] = NO_OBJ;
  g_desc[g] = NO_OBJ;
  return g;
}

// Add symbol s (a global variable if data, else a function) of the
// current object to the table of shared names. A second definition is
// an error, and so is a description that disagrees with the one kept;
// a more complete description replaces it.
static void add_global(Lsym *s, int data)
{
  int g = gsym_find(s->name);
  int count = data ? s->size : s->nparams, none = data ? 0 : 255;
  int kind = (data ? G_DATA : 0) | (s->flags & SYM_RETURNS ? G_RETURNS : 0);
  if (g < 0)
    g = gsym_add(s->name);
  if (g_desc[g] == NO_OBJ) {
    g_flags[g] = (unsigned char)kind;
    g_count[g] = count;
    g_hash[g] = s->hash;
    g_desc[g] = (unsigned char)cur;
  } else {
    if ((s->flags & SYM_DEFINED) && g_def[g] != NO_OBJ)
      between(220 /* "multiple definition of" */, s->name, g_def[g], cur);
    if ((g_flags[g] & (G_DATA | G_RETURNS)) != kind
        || (count != none && g_count[g] != none && count != g_count[g])
        || (s->hash && g_hash[g] && s->hash != g_hash[g]))
      between(221 /* "conflicting types for" */, s->name, g_desc[g], cur);
    if ((count != none && g_count[g] == none) || (s->hash && !g_hash[g])) {
      g_count[g] = count;
      g_hash[g] = s->hash;
      g_desc[g] = (unsigned char)cur;
    }
  }
  if (s->flags & SYM_DEFINED) {
    g_def[g] = (unsigned char)cur;
    g_off[g] = s->off;
    if (s->flags & SYM_BSS)
      g_flags[g] |= G_BSS;
  }
  if (s->flags & SYM_INDATA)
    g_flags[g] |= G_INDATA;
  if (s->label[0] && g_lab[g] < 0)  // an import, if nothing defines it
    g_lab[g] = strcmp(s->label, s->name) ? add_gname(s->label) : g_name[g];
  if (s->mod)
    g_mod[g] = (unsigned char)(obj_mod[s->mod - 1] + 1);
  if (s->flags & SYM_CALLED)
    g_flags[g] |= G_USED;
  if (s->flags & SYM_NOTCMDSYS)
    g_flags[g] |= G_BADCMDSYS;
  else if (!(s->flags & SYM_CMDSYS))
    g_flags[g] |= G_PLAIN;
}

// Place zero-filled global name, of size bytes: on the heap unless
// indata (an initializer holds its address). Returns its place: its
// moved number, or its offset after every object's data. The listing
// names each moved one, for tools/loops.py.
static int place_zero(const char *name, int size, int indata)
{
  if (indata) {
    zstay += size;
    if (zstay > 0x7FFF)
      fatal0(182 /* "too much data: globals over 32767 bytes" */);
    return zstay - size;
  }
  if (nmoved >= MAX_MOVED)
    fatal0(310 /* "too many large globals to move to the heap" */);
  if ((unsigned)size > 0xFFFFu - HEAP_SPARE - mv_total)
    fatal0(311 /* "too much data to move to the heap" */);
  mv_total += size;
  mv_size[nmoved] = size;
#ifdef CCISH_LISTING
  if (opt_list)
    printf("; heap global %d: %s, %d bytes\n", nmoved, name, size);
#else
  (void)name;
#endif
  return nmoved++;
}

// With -x: add the function at code-segment offset off to def_off[],
// keeping it in address order. Each object's functions come after the
// ones before it, but in its summary's order, not its code's.
static void add_def(unsigned off)
{
  int i = ndefs;
  if (i >= MAX_DEFS)
    fatal0(269 /* "too many functions for an XLOAD image" */);
  for (; i > 0 && def_off[i - 1] > off; i--)
    def_off[i] = def_off[i - 1];
  def_off[i] = off;
  ndefs++;
}

// Step 1 for object k: read its symbol summary, lay it out after the
// objects before it, place its own zero-filled globals, and add its
// non-local symbols to the table of shared names. A local symbol that is
// used but not defined is an undefined reference.
static void read_summary(int k)
{
  const char *name = open_syms(k);
  Lsym s;
  int i;

  obj_data[k + 1] = obj_data[k] + data_size;
  obj_strs[k + 1] = obj_strs[k] + strs_size;
  obj_code[k + 1] = obj_code[k] + code_size;
  obj_mv[k] = nmoved;
  obj_zs[k] = zstay;
  for (i = 0; i < nfsyms + ndsyms; i++) {
    read_sym(&s, i >= nfsyms, name);
    if ((s.flags & SYM_DEFINED) && i < nfsyms) {
      if (opt_xload)
        add_def(obj_code[k] + s.off);
      ndefined++;
    }
    if (!(s.flags & SYM_LOCAL))
      add_global(&s, i >= nfsyms);
    else if ((s.flags & SYM_CALLED) && !(s.flags & SYM_DEFINED))
      undefined(172 /* "undefined reference to" */, s.name);
    else if ((s.flags & (SYM_DEFINED | SYM_BSS)) == (SYM_DEFINED | SYM_BSS))
      place_zero(s.name, s.size, s.flags & SYM_INDATA);
  }
  close_syms(name, 1);
}

// Make CMDSYS function name, which the init function calls, used (an
// import, unless a program defines its own).
static void use_cmdsys(const char *name)
{
  int g = gsym_find(name);
  if (g < 0)
    g = gsym_add(name);
  g_flags[g] |= G_USED;
}

// After step 1: resolve the shared names. A used name that no object
// defines must be a CMDSYS function, which becomes an import; main, or
// __start for a main with parameters, becomes the entry point.
static void resolve(void)
{
  int g;
  for (g = 0; g < MAX_GSYMS; g++)
    if (g_name[g] >= 0 && (g_flags[g] & G_BSS))
      g_off[g] = place_zero(gnames + g_name[g], g_count[g],
                            g_flags[g] & G_INDATA);
  if (nmoved > 0) {
    use_cmdsys("__heapavail");
    use_cmdsys("__isult");
    use_cmdsys("__heapalloc");
    use_cmdsys("__memset");
    use_cmdsys("__puts");
  }
  for (g = 0; g < MAX_GSYMS; g++) {
    const char *name;
    int fl = g_flags[g];
    if (g_name[g] < 0)
      continue;
    name = gnames + g_name[g];
    if (g_def[g] != NO_OBJ) {
      if (!(fl & G_DATA) && !strcmp(name, "main"))
        main_sym = g;
    } else if (fl & G_USED) {
      // Not defined: a CMDSYS function, or one declared as a PLASMA
      // import (an asm label, or a pragma's module), is imported.
      if (g_lab[g] < 0 && (fl & G_BADCMDSYS))
        undefined(173 /* "declaration doesn't match the CMDSYS"
                         " function" */, name);
      if (g_lab[g] < 0 && (fl & (G_DATA | G_PLAIN)))
        undefined(172 /* "undefined reference to" */, name);
      if (nimports >= 255)
        fatal0(190 /* "too many imported functions" */);
      g_flags[g] = (unsigned char)(fl | G_IMPORT);
      g_off[g] = nimports++;
      if (g_mod[g])
        mod_used[g_mod[g] - 1] = 1;
    }
  }
  // main(argc, argv): init calls the library's startup (lib/start.c),
  // which splits the command line and calls main.
  if (main_sym >= 0 && g_count[main_sym] == 2) {
    g = gsym_find("__start");
    if (g < 0 || g_def[g] == NO_OBJ)
      undefined(172 /* "undefined reference to" */, "__start");
    else
      main_sym = g;
  }
}

#ifdef CCISH_LISTING
// For the listing: where function symbol s of the current object
// (summary file name) has its name in gnames[], adding a static one
// after the shared names if it fits; or -1.
static int list_name(Lsym *s, const char *name)
{
  int g, n, at;
  if (!(s->flags & SYM_LOCAL)) {
    if ((g = gsym_find(s->name)) < 0)
      damaged(name);
    return g_name[g];
  }
  n = (int)strlen(s->name) + 1;
  if (!(s->flags & SYM_DEFINED) || lnames_len + n > MAX_GNAMES)
    return -1;
  at = lnames_len;
  memcpy(gnames + at, s->name, n);
  lnames_len += n;
  return at;
}
#endif

// Steps 2 and 3: read object k's symbol summary again, into fmap[] and
// dmap[], and fname[] if listing.
static void map_object(int k)
{
  const char *name = open_syms(k);
  Lsym s;
  int i, g, v, mv = obj_mv[k], zs = obj_zs[k];

#ifdef CCISH_LISTING
  lnames_len = gnames_len;
#endif
  for (i = 0; i < nfsyms + ndsyms; i++) {
    int data = i >= nfsyms;
    read_sym(&s, data, name);
    v = -1;
    g = -1;
    if (!(s.flags & SYM_LOCAL) && ((g = gsym_find(s.name)) < 0))
      damaged(name);
    if (data && (s.flags & (SYM_DEFINED | SYM_BSS)) == (SYM_DEFINED | SYM_BSS)
        && g < 0) {
      // A zero-filled static, placed in step 1's order.
      v = s.flags & SYM_INDATA ? obj_data[nobjs] + zs : -2 - mv;
      if (s.flags & SYM_INDATA)
        zs += s.size;
      else
        mv++;
    } else if (data && g >= 0 && (g_flags[g] & G_BSS)) {
      v = g_flags[g] & G_INDATA ? obj_data[nobjs] + g_off[g]
                                : -2 - g_off[g];
    } else if (s.flags & SYM_DEFINED) {
      v = s.off + (data ? obj_data[k] : obj_code[k]);
    } else if (g >= 0) {
      if (g_def[g] != NO_OBJ)
        v = g_off[g] + (data ? obj_data[g_def[g]] : obj_code[g_def[g]]);
      else if (g_flags[g] & G_IMPORT)
        v = (data ? DM_IMPORT : -2) - g_off[g];
    }
    if (data) {
      dmap[i - nfsyms] = v;
    } else {
      fmap[i] = v;
#ifdef CCISH_LISTING
      if (opt_list)
        fname[i] = list_name(&s, name);
#endif
    }
  }
  close_syms(name, 0);
}

// ---------- Listing ----------
#ifdef CCISH_LISTING

// Operand formats for the disassembler.
enum { F_NONE, F_BYTE, F_WORD, F_REL, F_ENTER, F_STR };

static const struct { const char *name; int fmt; } opinfo[0xC6 / 2] = {
  // $00-$1E: CN 0..15
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  // $20
  {"MINUS1", F_NONE}, {"BREQ", F_REL}, {"BRNE", F_REL}, {"LA", F_WORD},
  {"LLA", F_BYTE}, {"CB", F_BYTE}, {"CW", F_WORD}, {"CS", F_STR},
  // $30
  {"DROP", F_NONE}, {"DROP2", F_NONE}, {"DUP", F_NONE}, {"DIVMOD", F_NONE},
  {"ADDI", F_BYTE}, {"SUBI", F_BYTE}, {"ANDI", F_BYTE}, {"ORI", F_BYTE},
  // $40
  {"ISEQ", F_NONE}, {"ISNE", F_NONE}, {"ISGT", F_NONE}, {"ISLT", F_NONE},
  {"ISGE", F_NONE}, {"ISLE", F_NONE}, {"BRFLS", F_REL}, {"BRTRU", F_REL},
  // $50
  {"BRNCH", F_REL}, {"SEL", F_REL}, {"CALL", F_WORD}, {"ICAL", F_NONE},
  {"ENTER", F_ENTER}, {"LEAVE", F_BYTE}, {"RET", F_NONE}, {"CFFB", F_BYTE},
  // $60
  {"LB", F_NONE}, {"LW", F_NONE}, {"LLB", F_BYTE}, {"LLW", F_BYTE},
  {"LAB", F_WORD}, {"LAW", F_WORD}, {"DLB", F_BYTE}, {"DLW", F_BYTE},
  // $70
  {"SB", F_NONE}, {"SW", F_NONE}, {"SLB", F_BYTE}, {"SLW", F_BYTE},
  {"SAB", F_WORD}, {"SAW", F_WORD}, {"DAB", F_WORD}, {"DAW", F_WORD},
  // $80
  {"NOT", F_NONE}, {"ADD", F_NONE}, {"SUB", F_NONE}, {"MUL", F_NONE},
  {"DIV", F_NONE}, {"MOD", F_NONE}, {"INCR", F_NONE}, {"DECR", F_NONE},
  // $90
  {"NEG", F_NONE}, {"COMP", F_NONE}, {"AND", F_NONE}, {"IOR", F_NONE},
  {"XOR", F_NONE}, {"SHL", F_NONE}, {"SHR", F_NONE}, {"IDXW", F_NONE},
  // $A0
  {"BRGT", F_REL}, {"BRLT", F_REL}, {"INCBRLE", F_REL}, {"ADDBRLE", F_REL},
  {"DECBRGE", F_REL}, {"SUBBRGE", F_REL}, {"BRAND", F_REL}, {"BROR", F_REL},
  // $B0
  {"ADDLB", F_BYTE}, {"ADDLW", F_BYTE}, {"ADDAB", F_WORD}, {"ADDAW", F_WORD},
  {"IDXLB", F_BYTE}, {"IDXLW", F_BYTE}, {"IDXAB", F_WORD}, {"IDXAW", F_WORD},
  // $C0
  {"NATV", F_NONE}, {"JUMPZ", F_WORD}, {"JUMP", F_WORD}
};

// The listing's name for function symbol t of the current object: its
// name, or static@$ and its load address if the name didn't fit.
static const char *func_label(int t)
{
  static char buf[16];
  if (fname[t] >= 0)
    return gnames + fname[t];
  sprintf(buf, "static@$%04X", subseg + fmap[t]);
  return buf;
}

// The comment for a relocated word at load address at, from the
// relocations of the function being listed.
static const char *reloc_note(int at)
{
  static char buf[MAX_NAME + 16];
  int i, t;
  for (i = 0; i < nlr; i++)
    if (lr_at[i] == at) {
      t = lr_target[i];
      if (lr_kind[i] == REL_DATA) {
        sprintf(buf, "  ; data+%d", t);
      } else if (lr_kind[i] == REL_STR) {
        sprintf(buf, "  ; string+%d", t);
      } else if (lr_kind[i] == REL_HELPER && t >= NHELPERS) {
        sprintf(buf, "  ; %s (import)", h_cmdsys[t - NHELPERS]);
      } else if (lr_kind[i] == REL_HELPER) {
        sprintf(buf, "  ; helper %s", h_name[t]);
      } else if (lr_kind[i] == REL_DSYM) {
        // An imported global: t is its import index.
        strcpy(buf, "  ; ");
        for (t = 0; t < MAX_GSYMS; t++)
          if (g_name[t] >= 0 && (g_flags[t] & G_IMPORT)
              && g_off[t] == lr_target[i])
            strcat(buf, gnames + g_name[t]);
        strcat(buf, " (import)");
      } else {
        // t < 0: init's call to shared name -2 - t.
        strcpy(buf, "  ; ");
        strcat(buf, t < 0 ? gnames + g_name[-2 - t] : func_label(t));
        if (t >= 0 ? fmap[t] < -1 : (g_flags[-2 - t] & G_IMPORT) != 0)
          strcat(buf, " (import)");
      }
      return buf;
    }
  return "";
}

// The signed 16-bit word at fbuf offset at, low byte first. The
// arithmetic gives the same value whether int has 16 or 32 bits.
static int signed_at(int at)
{
  return ((fbuf[at] | (fbuf[at + 1] << 8)) ^ 0x8000) - 0x8000;
}

// List the case table at fbuf offset pc (code-segment offset start +
// pc): its count, then each case's value and where it goes. Returns its
// size in bytes.
static int list_table(int pc, int start)
{
  int n = fbuf[pc], i, at, w;
  printf("  %04X  %02X        CASES %d\n", subseg + start + pc, n, n);
  for (i = 0; i < n; i++) {
    at = pc + 1 + 4 * i;
    w = signed_at(at);
    printf("  %04X  %02X %02X     CASE %d\n", subseg + start + at,
           fbuf[at], fbuf[at + 1], w);
    at += 2;
    w = signed_at(at);
    printf("  %04X  %02X %02X     TO %+d  ; to %04X\n",
           subseg + start + at, fbuf[at], fbuf[at + 1], w,
           subseg + start + at + w);
  }
  return 1 + 4 * n;
}

// List len bytes of code (in fbuf) that start at code-segment offset
// start; addresses are shown as loaded at subseg. A SEL's case table is
// data inside the code, so each SEL's target is noted, and the table is
// listed as a table when the listing reaches it. Its relocations are
// in lr_at[] and the rest.
static void list_code(const char *label, int len, int start)
{
  int pc = 0, i, tables[MAX_SEL_TABLES], ntables = 0;
  printf("%s:\n", label);
  while (pc < len) {
    int op = fbuf[pc], n = 1, fmt;
    for (i = 0; i < ntables && tables[i] != pc; i++)
      ;
    if (i < ntables) {
      tables[i] = tables[--ntables];
      pc += list_table(pc, start);
      continue;
    }
    if (op & 1 || op >= 0xC6) {
      printf("  %04X  %02X        ???\n", subseg + start + pc, op);
      pc++;
      continue;
    }
    fmt = opinfo[op / 2].fmt;
    if (fmt == F_BYTE)
      n = 2;
    else if (fmt >= F_WORD && fmt <= F_ENTER)
      n = 3;
    else if (fmt == F_STR)
      n = 2 + fbuf[pc + 1];
    printf("  %04X  ", subseg + start + pc);
    for (i = 0; i < 3; i++)
      if (i < n)
        printf("%02X ", fbuf[pc + i]);
      else
        printf("   ");
    printf("%s", opinfo[op / 2].name);
    switch (fmt) {
    case F_NONE:
      if (op < 0x20)
        printf(" %d", op / 2);
      break;
    case F_BYTE:
      printf(" %d", fbuf[pc + 1]);
      break;
    case F_WORD:
      printf(" $%04X%s", fbuf[pc + 1] | (fbuf[pc + 2] << 8),
             reloc_note(subseg + start + pc + 1));
      break;
    case F_REL: {
      // The offset is relative to the offset word itself.
      int off = signed_at(pc + 1);
      printf(" %+d  ; to %04X", off, subseg + start + pc + 1 + off);
      if (op == OP_SEL && ntables < MAX_SEL_TABLES)
        tables[ntables++] = pc + 1 + off;
      break;
    }
    case F_ENTER:
      printf(" %d,%d", fbuf[pc + 1], fbuf[pc + 2]);
      break;
    case F_STR:
      printf(" (%d chars)", fbuf[pc + 1]);
      break;
    }
    printf("\n");
    pc += n;
  }
}

// Keep a relocation of the word at load address at for the listing
// (lr_at[] and the rest).
static void keep_reloc(int kind, int at, int target)
{
  if (nlr < MAX_FUNC_RELOCS) {
    lr_at[nlr] = at;
    lr_kind[nlr] = (char)kind;
    lr_target[nlr] = target;
    nlr++;
  }
}
#endif // CCISH_LISTING

// ---------- Steps 2-4: the module ----------

// The word that refers to the function at code-segment offset off: its
// address, or with -x, its number in the function table, found in
// def_off[] by a binary search.
static int func_word(unsigned off)
{
  int lo = 0, hi = ndefs - 1, mid;
  if (!opt_xload)
    return subseg + (int)off;
  while (lo <= hi) {
    mid = (lo + hi) >> 1;
    if (def_off[mid] == off)
      return mid;
    if (def_off[mid] < off)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  bad_obj();
  return 0;
}

// Write a name in DCI form: uppercase, high bit set on every character
// but the last. PLASMA keeps 16 significant characters.
static void out_dci(const char *name)
{
  int n = (int)strlen(name), i;
  if (n > 16)
    n = 16;
  for (i = 0; i < n; i++) {
    int c = name[i];
    if (c >= 'a' && c <= 'z')
      c -= 'a' - 'A';
    bputc(&out, i < n - 1 ? c | 0x80 : c);
  }
}

// Return the final value of the word at load address at, given a
// relocation read from the current object's stream; keep it for the
// listing if list. The record is checked first. Its target is an
// offset in the object's data or strings, or a symbol of the object's,
// through fmap[] and dmap[]. A REL_DSYM (data symbol target, plus add)
// becomes the REL_DATA it stands for: for a global moved to the heap
// (dmap[] -2 - its number), its pointer word's, and patch_law is set,
// since the LA before the word must become an LAW.
static int add_reloc(int kind, int at, int target, int add, int list)
{
  int v;
  if (kind == REL_DSYM && target < ndsyms && IS_DIMPORT(dmap[target])) {
    // An imported global: the word holds the addend, which the loader
    // adds the symbol's address to (reloc_entry()).
#ifdef CCISH_LISTING
    if (list)
      keep_reloc(REL_DSYM, at, DM_IMPORT - dmap[target]);
#endif
    return add;
  }
  if (kind == REL_DSYM && target < ndsyms && dmap[target] < -1) {
    if (add != 0)
      bad_obj();
    kind = REL_DATA;
    target = obj_data[nobjs] + zstay + 2 * (-2 - dmap[target]);
    patch_law = 1;
  } else if (kind == REL_DSYM) {
    if (target >= ndsyms || dmap[target] < 0)
      bad_obj();
    kind = REL_DATA;
    target = (dmap[target] + add) & 0xFFFF;
  } else if (kind == REL_DATA) {
    // Any offset, wrapped: ccgen folds a[i - 1]'s -1 into a's address
    // (peep.c, rewrite_index), which the index brings back inside.
    target = (target + obj_data[cur]) & 0xFFFF;
  } else if (kind == REL_STR) {
    target = (target + obj_strs[cur]) & 0xFFFF;
  } else if (kind == REL_HELPER && target >= NHELPERS) {
    if (target >= NHELPERS + NHELP_CMDSYS)
      bad_obj();                // a CMDSYS comparison: an import
  } else if (kind == REL_HELPER) {
    if (h_def[target] == NO_OBJ)
      bad_obj();
  } else if (kind != REL_FUNC || target >= nfsyms || fmap[target] == -1) {
    bad_obj();
  }
#ifdef CCISH_LISTING
  if (list)
    keep_reloc(kind, at, target);
#else
  (void)at;
  (void)list;
#endif
  if (kind == REL_DATA)
    return data_base + target;
  if (kind == REL_STR)
    return strs_base + target;
  if (kind == REL_HELPER)
    return target >= NHELPERS ? 0 : func_word(h_off[target]);
  v = fmap[target];
  return v < 0 ? 0 : func_word(v);  // an import: the loader fills it in
}

// The tags each step reads from the stream; it skips the others.
static const char data_tags[] = { REC_DATA, REC_DREL, REC_ZERO, 0 };
static const char str_tags[] = { REC_STR, 0 };
static const char func_tags[] = { REC_FUNC, 0 };
static const char rld_tags[] = { REC_DATA, REC_DREL, REC_ZERO, REC_FUNC, 0 };

// Open object k's stream file for reading, from its start.
static void open_obj(int k)
{
  const char *name = stream_name(obj_name[k], SUFFIX_OBJ);
  if (!bopen(&in, name, 0))
    fatal_d(171 /* "cannot open" */, name);
}

// Read past the rest of a record whose tag t has just been read.
static void skip_record(int t)
{
  int n;
  if (t == REC_DATA) {
    if ((n = bgetc(&in)) == EOF)
      bad_obj();
    bskip(&in, n);
  } else if (t == REC_DREL) {
    bskip(&in, bgetc(&in) == REL_DSYM ? 4 : 2);
  } else if (t == REC_STR) {
    bskip(&in, bgetw(&in));
  } else if (t == REC_FUNC) {
    bskip(&in, 2);
    bskip(&in, bgetw(&in));
    if ((n = bgetw(&in)) > MAX_FUNC_RELOCS)
      bad_obj();
    bskip(&in, 5 * n);
  } else if (t == REC_HELPER) {
    bskip(&in, 1);
    bskip(&in, bgetw(&in));
  } else if (t == REC_ZERO) {
    bskip(&in, 2);
  } else {
    bad_obj();
  }
}

// Read the next record's tag that is one of want, skipping records with
// other tags; or return EOF at the end of the stream.
static int next_record(const char *want)
{
  int t;
  while ((t = bgetc(&in)) != EOF && (t == 0 || !strchr(want, t)))
    skip_record(t);
  return t;
}

// Copy the next n bytes of the stream into the module.
static void copy_bytes(int n)
{
  int c;
  while (n-- > 0) {
    if ((c = bgetc(&in)) == EOF)
      bad_obj();
    bputc(&out, c);
  }
}

// Copy object k's data into the module: its stream's D records, each
// Z record's zeros, and each R record's word given its final value (an
// address, kept for the RLD).
static void copy_data(int k)
{
  int at = 0, t, n, kind, target, add;
  map_object(k);
  open_obj(k);
  while ((t = next_record(data_tags)) != EOF) {
    if (t == REC_DATA) {
      if ((n = bgetc(&in)) == EOF || at + n > data_size)
        bad_obj();
      copy_bytes(n);
      at += n;
    } else if (t == REC_ZERO) {
      if ((n = bgetw(&in)) > data_size - at)
        bad_obj();
      at += n;
      while (n-- > 0)
        bputc(&out, 0);
    } else {
      kind = bgetc(&in);
      target = bgetw(&in);
      add = kind == REL_DSYM ? bgetw(&in) : 0;
      if (at + 2 > data_size)
        bad_obj();
      bputw(&out, add_reloc(kind, data_base + obj_data[k] + at, target,
                            add, 0));
      if (patch_law)
        bad_obj();              // a moved global's address in data
      at += 2;
    }
  }
  bclose(&in);
  if (at != data_size)
    bad_obj();
}

// Copy object k's string pool into the module: each S record's string
// as [len][chars][NUL]. A string longer than 255 gets a length byte of
// 0, which is meaningless (pstr() rejects such a string).
static void copy_strings(int k)
{
  int at = 0, n, size = obj_strs[k + 1] - obj_strs[k];
  cur = k;
  open_obj(k);
  while (next_record(str_tags) != EOF) {
    n = bgetw(&in);
    if (n > size - at - 2)
      bad_obj();
    bputc(&out, n > 255 ? 0 : n);
    copy_bytes(n);
    bputc(&out, 0);
    at += n + 2;
  }
  bclose(&in);
  if (at != size)
    bad_obj();
}

// Copy object k's code into the module, one F record (function) at a
// time.
static void copy_code(int k)
{
  int at = 0, idx, len, n, i, c, start;
  unsigned char *p, *end;

  map_object(k);
#ifdef CCISH_LISTING
  if (opt_list && nobjs > 1)
    printf("object %s:\n", obj_name[k]);
#endif
  open_obj(k);
  while (next_record(func_tags) != EOF) {
    idx = bgetw(&in);
    len = bgetw(&in);
    if (idx >= nfsyms || fmap[idx] != obj_code[k] + at
        || len > MAX_FUNC_CODE)
      bad_obj();
    end = fbuf + len;
    for (p = fbuf; p != end; p++) {
      if ((c = bgetc(&in)) == EOF)
        bad_obj();
      *p = (unsigned char)c;
    }
    start = obj_code[k] + at;
#ifdef CCISH_LISTING
    nlr = 0;
#endif
    if ((n = bgetw(&in)) > MAX_FUNC_RELOCS)
      bad_obj();
    for (i = 0; i < n; i++) {
      int kind = bgetc(&in), off = bgetw(&in), target = bgetw(&in), v;
      if (off < 0 || off > len - 2)
        bad_obj();
      // The word holds an extern global's addend, else 0.
      p = fbuf + off;
      v = add_reloc(kind, subseg + start + off, target,
                    p[0] | (p[1] << 8), opt_list);
      if (patch_law) {
        // A moved global: LA its address becomes LAW its pointer.
        patch_law = 0;
        if (off < 1 || p[-1] != OP_LA)
          bad_obj();
        p[-1] = OP_LAW;
      }
      p[0] = (unsigned char)v;
      p[1] = (unsigned char)(v >> 8);
    }
#ifdef CCISH_LISTING
    if (opt_list)
      list_code(func_label(idx), len, start);
#endif
    bwrite(&out, fbuf, len);
    at += len;
  }
  bclose(&in);
  if (at != code_size)
    bad_obj();
}

// The tag step 3 reads for the helpers.
static const char helper_tags[] = { REC_HELPER, 0 };

// Copy each helper the module holds into it, after every object's code:
// the H record of the first object that holds it.
static void copy_helpers(void)
{
  int k, t, len, i, c;
  for (k = 0; k < NHELPERS; k++) {
    if (h_def[k] == NO_OBJ)
      continue;
    cur = h_def[k];
    open_obj(cur);
    for (;;) {
      if (next_record(helper_tags) == EOF)
        bad_obj();
      t = bgetc(&in);
      len = bgetw(&in);
      if (t == k)
        break;
      bskip(&in, len);
    }
    if (len != h_size[k] || len > MAX_FUNC_CODE)
      bad_obj();
    for (i = 0; i < len; i++) {
      if ((c = bgetc(&in)) == EOF)
        bad_obj();
      fbuf[i] = (unsigned char)c;
    }
    bclose(&in);
#ifdef CCISH_LISTING
    if (opt_list) {
      static char label[MAX_HNAME + 8];
      nlr = 0;
      strcpy(label, "helper ");
      strcat(label, h_name[k]);
      list_code(label, len, h_off[k]);
    }
#endif
    bwrite(&out, fbuf, len);
  }
}

// ---------- Step 4: the RLD, read again from the objects ----------

// The kinds of fixup in an XLOAD image, besides an import's number.
#define XL_ADD (-1)             // the load address added
#define XL_DEF (-2)             // a function's number, which becomes its
                                // def entry's address

// With -x, write the fixup of the word at body offset at, of kind fx
// (XL_ADD, XL_DEF, or an import's number). The fixups must come in
// address order, and each is written as the gap from the word before:
// a byte of kind and gap (k + 0 to most), or the kind's escape (k +
// most + 1) and the gap as a word.
static void xl_fixup(int fx, int at)
{
  unsigned gap = (unsigned)at - fx_at, most = 0x3E;
  int k = fx == XL_DEF ? 0x40 : 0;
  if ((unsigned)at < fx_at)
    fatal0(275 /* "relocations out of order for an XLOAD image" */);
  if (fx >= 0) {
    bputc(&out, 0x80 + fx);         // and then its gap
    most = 0xFE;
  }
  if (gap <= most) {
    bputc(&out, k + (int)gap);
  } else {
    bputc(&out, k + (int)most + 1);
    bputw(&out, (int)gap);
  }
  fx_at = (unsigned)at + 2;
}

// Write one 4-byte RLD entry: its type, a word, and a byte.
static void rld_entry(int type, int word, int byte)
{
  bputc(&out, type);
  bputw(&out, word);
  bputc(&out, byte);
}

// Write the RLD's function entries for object k, one for each function
// its summary defines. The loaders want them all before any
// relocation, but in any order (cmd.pla, cmdsys.c), so the summary,
// much smaller than the stream, is read again, not the F records.
static void def_entries(int k)
{
  const char *name = open_syms(k);
  Lsym s;
  int i;

  for (i = 0; i < nfsyms + ndsyms; i++) {
    read_sym(&s, i >= nfsyms, name);
    if (i < nfsyms && (s.flags & SYM_DEFINED))
      rld_entry(0x02, subseg + obj_code[k] + s.off, 0);
  }
  close_syms(name, 0);
}

// Write the RLD entry for a relocation of kind to target (a function
// symbol of the current object, for REL_FUNC; a data symbol, for
// REL_DSYM) of the word at load address at: an import's (a function or
// an imported global), with its ESD index, or an address's.
static void reloc_entry(int kind, int target, int at)
{
  int v = -1;
  if (kind == REL_FUNC) {
    if (target >= nfsyms)
      bad_obj();
    v = fmap[target];
  } else if (kind == REL_DSYM && target < ndsyms
             && IS_DIMPORT(dmap[target])) {
    v = -2 - (DM_IMPORT - dmap[target]);    // an imported global
  } else if (kind == REL_HELPER && target >= NHELPERS) {
    v = -2 - g_off[gsym_find(h_cmdsys[target - NHELPERS])];   // CMDSYS
  }
  if (opt_xload)
    xl_fixup(v < -1 ? -2 - v
             : kind == REL_FUNC || kind == REL_HELPER ? XL_DEF : XL_ADD, at);
  else if (v < -1)
    rld_entry(0x91, at - MODULE_BASE, -2 - v);
  else
    rld_entry(0x81, at - MODULE_BASE, 0);
}

// Write the RLD's relocations for object k: read its stream again, for
// the R records (if data) and each F record's relocations (if code),
// which steps 2 and 3 have checked.
static void reloc_entries(int k, int data, int code)
{
  int at = 0, start = obj_code[k], t, n, kind, target, off, len;

  map_object(k);
  open_obj(k);
  while ((t = next_record(rld_tags)) != EOF) {
    if (t == REC_DATA) {
      n = bgetc(&in);
      bskip(&in, n);
      at += n;
    } else if (t == REC_ZERO) {
      at += bgetw(&in);
    } else if (t == REC_DREL) {
      kind = bgetc(&in);
      target = bgetw(&in);
      if (kind == REL_DSYM)
        bskip(&in, 2);                    // its addend
      if (data)
        reloc_entry(kind, target, data_base + obj_data[k] + at);
      at += 2;
    } else {
      bskip(&in, 2);                      // function symbol index
      len = bgetw(&in);
      bskip(&in, len);
      if ((n = bgetw(&in)) > MAX_FUNC_RELOCS)
        bad_obj();
      while (n-- > 0) {
        kind = bgetc(&in);
        off = bgetw(&in);
        target = bgetw(&in);
        if (code)
          reloc_entry(kind, target, subseg + start + off);
      }
      start += len;
    }
  }
  bclose(&in);
}

// What init prints if the heap can't hold the moved globals.
static const char no_memory[] = "not enough memory\r";

// The init function's relocated words: each one's fbuf offset, and the
// shared name it calls, or -1 for a pointer word's address.
#define MAX_INIT_RELOCS (MAX_MOVED + 8)
static int ir_at[MAX_INIT_RELOCS];
static int ir_g[MAX_INIT_RELOCS];
static int nir;

// Append op and its word w to the init function in fbuf at *n; the
// word is relocated, to a call of shared name g, or (g -1) to an
// address in data.
static void init_op(int *n, int op, int w, int g)
{
  if (g >= 0)
    w = g_flags[g] & G_IMPORT ? 0
        : func_word(obj_code[g_def[g]] + g_off[g]);
  ir_at[nir] = *n + 1;
  ir_g[nir++] = g;
  fbuf[(*n)++] = (unsigned char)op;
  fbuf[(*n)++] = (unsigned char)w;
  fbuf[(*n)++] = (unsigned char)(w >> 8);
}

// Append op and its unrelocated word w to the init function.
static void init_word(int *n, int op, int w)
{
  fbuf[(*n)++] = (unsigned char)op;
  fbuf[(*n)++] = (unsigned char)w;
  fbuf[(*n)++] = (unsigned char)(w >> 8);
}

// The init function runs when the module loads. If globals were moved
// to the heap, it checks that the heap holds their block and
// HEAP_SPARE more (or prints no_memory and returns -1, an error), takes
// the block from heapalloc, zeroes it, and points each pointer word at
// its global. (It asks heapavail first: plvm's heapalloc stops the VM
// when the heap is full, and the Apple's can wrap past $FFFF.) Then it
// calls main and returns -(status & 255), main's exit status as a
// PLASMA error (0 for success, or for a void main), never positive: a
// positive init result would be read as "keep the module" flags. CMD
// reports a negative one as ERR:$xx, and tools/ccstat.pla hands it to
// the host (approach.md issue 22). Init must be the last bytecode.
static int init_code(void)
{
  int n = 0, i, pw = data_base + obj_data[nobjs] + zstay;
  nir = 0;
  if (nmoved > 0) {
    init_op(&n, OP_CALL, 0, gsym_find("__heapavail"));
    init_word(&n, OP_CW, (int)mv_total + HEAP_SPARE);
    init_op(&n, OP_CALL, 0, gsym_find("__isult"));
    // To ok: past this word, the CS, CALL, MINUS1 and RET.
    init_word(&n, OP_BRFLS, 2 + (1 + sizeof(no_memory)) + 3 + 1 + 1);
    fbuf[n++] = OP_CS;
    fbuf[n++] = sizeof(no_memory) - 1;
    for (i = 0; no_memory[i]; i++)
      fbuf[n++] = no_memory[i];
    init_op(&n, OP_CALL, 0, gsym_find("__puts"));
    fbuf[n++] = OP_MINUS1;
    fbuf[n++] = OP_RET;
    // ok:
    init_word(&n, OP_CW, (int)mv_total);
    init_op(&n, OP_CALL, 0, gsym_find("__heapalloc"));
    init_op(&n, OP_DAW, pw, -1);
    for (i = 1; i < nmoved; i++) {
      init_word(&n, OP_CW, mv_size[i - 1]);
      fbuf[n++] = OP_ADD;
      init_op(&n, OP_DAW, pw + 2 * i, -1);
    }
    fbuf[n++] = OP_DROP;
    init_op(&n, OP_LAW, pw, -1);
    fbuf[n++] = OP_CN;
    init_word(&n, OP_CW, (int)mv_total);
    init_op(&n, OP_CALL, 0, gsym_find("__memset"));
  }
  init_op(&n, OP_CALL, 0, main_sym);
  if (g_flags[main_sym] & G_RETURNS) {
    fbuf[n++] = OP_ANDI;
    fbuf[n++] = 255;
    fbuf[n++] = OP_NEG;
  } else
    fbuf[n++] = OP_CN;
  fbuf[n++] = OP_RET;
  return n;
}

// Bytes in the init function that init_code builds, or 0 if there is
// no main.
static int init_size(void)
{
  int n;
  if (main_sym < 0)
    return 0;
  n = g_flags[main_sym] & G_RETURNS ? 7 : 5;
  if (nmoved > 0)
    n += 3 + 3 + 3 + 3 + 1 + sizeof(no_memory) + 3 + 1 + 1
         + 3 + 3 + 3 + 7 * (nmoved - 1) + 1 + 3 + 1 + 3 + 3;
  return n;
}

// Write the RLD entries (or with -x, the fixups) for the init function
// at code-segment offset init_off.
static void init_entries(int init_off)
{
  int i, at, g, imp;
  for (i = 0; i < nir; i++) {
    at = subseg + init_off + ir_at[i];
    g = ir_g[i];
    imp = g >= 0 && (g_flags[g] & G_IMPORT) ? g_off[g] : -1;
    if (opt_xload)
      xl_fixup(imp >= 0 ? imp : g >= 0 ? XL_DEF : XL_ADD, at);
    else if (imp >= 0)
      rld_entry(0x91, at - MODULE_BASE, imp);
    else
      rld_entry(0x81, at - MODULE_BASE, 0);
  }
}

// Write the imports' names, in import order (slot order), each in DCI
// form, then $00: the ESD, with each one's type and number unless -x.
// One declared as a PLASMA import has its PLASMA name; the others are
// CMDSYS functions, whose C name is the PLASMA name with "__" in front
// (__puts for PUTS), so the "__" goes.
static void write_imports(void)
{
  int i;
  for (i = 0; i < MAX_GSYMS; i++)
    if (g_name[i] >= 0 && (g_flags[i] & G_IMPORT)) {
      const char *name = gnames + (g_lab[i] >= 0 ? g_lab[i] : g_name[i]);
      if (g_lab[i] < 0 && name[0] == '_' && name[1] == '_')
        name += 2;
      out_dci(name);
#ifdef CCISH_LISTING
      if (opt_list)
        printf("; import %s as %s\n", gnames + g_name[i], name);
#endif
      if (!opt_xload) {
        bputc(&out, 0x10);
        bputw(&out, g_off[i]);
      }
    }
  bputc(&out, 0);
}

#ifdef CCISH_LISTING
// Keep init's relocations, of len bytes at code-segment offset
// init_off, and list it. A function of its own, as list_summary() is,
// so that link_module() stays within MAX_FUNC_RELOCS in the full build.
static void list_init(int len, int init_off)
{
  int i;
  nlr = 0;
  for (i = 0; i < nir; i++)
    if (ir_g[i] < 0)
      keep_reloc(REL_DATA, subseg + init_off + ir_at[i],
                 (fbuf[ir_at[i]] | (fbuf[ir_at[i] + 1] << 8))
                 - data_base);
    else
      keep_reloc(REL_FUNC, subseg + init_off + ir_at[i], -2 - ir_g[i]);
  if (opt_list)
    list_code("(init)", len, init_off);
}

// List the modules loaded first, named dep and the used ones, and how
// full the shared-name table is, to see a limit coming.
static void list_summary(const char *dep)
{
  int i;
  if (!opt_list)
    return;
  printf("; dependencies:%s", nimports ? " CMDSYS" : "");
  for (i = 0; i < nmods; i++)
    if (mod_used[i] && strcmp(mod_name[i], dep))
      printf(" %s", mod_name[i]);
  printf("\n");
  printf("; shared names: %d of %d slots, %d of %d name bytes\n",
         ngsyms, MAX_GSYMS, gnames_len, MAX_GNAMES);
}
#endif

// Write the module's header, of a module whose code segment holds
// total bytes, init's from init_off, and its dependencies (CMDSYS, as
// dep, if anything is imported, then each module a used import comes
// from); with -x, the image's header, dependencies and imports, and its
// function table.
static void write_header(const char *dep, int init_off, int total)
{
  int i;
  if (opt_xload) {
    bputc(&out, 'X');                     // magic
    bputc(&out, 'L');
    bputc(&out, 2);                       // version
    bputc(&out, 0);                       // flags
    bputw(&out, subseg + total);          // the body's length
    bputw(&out, subseg);                  // where its bytecode starts
    bputw(&out, ndefs);
  } else {
    bputw(&out, subseg - MODULE_BASE + total);
    bputw(&out, 0x6502);                  // magic
    bputw(&out, 0);                       // system flags
    bputw(&out, subseg);
    bputw(&out, ndefined + nhelpers + (main_sym >= 0));
    bputw(&out, main_sym >= 0 ? subseg + init_off : 0);
  }
  if (nimports)
    out_dci(dep);
  for (i = 0; i < nmods; i++)
    if (mod_used[i] && strcmp(mod_name[i], dep))
      out_dci(mod_name[i]);
  bputc(&out, 0);                         // end of dependencies
  if (opt_xload) {
    write_imports();
    for (i = 0; i < ndefs; i++)           // the function table
      bputw(&out, subseg + (int)def_off[i]);
  }
}

// Step 4 for the n objects, init's code at init_off. The RLD: function
// entries first (init excluded), then the relocations, both read again
// from the objects; then init's call to main; then the ESD. Or with
// -x, the fixups, in address order: every object's data's, every
// object's code's, then init's.
static void write_relocs(int n, int init_off)
{
  int i, k;
  if (opt_xload) {
    fx_at = 0;
    for (k = 0; k < n; k++)
      reloc_entries(k, 1, 0);
    for (k = 0; k < n; k++)
      reloc_entries(k, 0, 1);
    init_entries(init_off);
    bputc(&out, 0xFF);
    return;
  }
  for (k = 0; k < n; k++)
    def_entries(k);
  for (i = 0; i < NHELPERS; i++)
    if (h_def[i] != NO_OBJ)
      rld_entry(0x02, subseg + h_off[i], 0);
  for (k = 0; k < n; k++)
    reloc_entries(k, 1, 1);
  if (main_sym >= 0)
    init_entries(init_off);
  bputc(&out, 0);
  write_imports();
}

// Run the final pass: link the n objects whose output names (as given
// to cccomp) are bases, in that order, and write the REL module to
// path.
void link_module(int n, const char **bases, const char *path)
{
  static const char dep[] = "CMDSYS";
  int hdr = 10;
  int init_off, deplen, total, i, k;

  // Step 1.
  if (n > MAX_OBJS)
    fatal0(223 /* "too many objects to link" */);
  nobjs = n;
  for (i = 0; i < MAX_GSYMS; i++)
    g_name[i] = -1;
  for (i = 0; i < NHELPERS; i++)
    h_def[i] = NO_OBJ;
  nhelpers = 0;
  for (k = 0; k < n; k++) {
    obj_name[k] = bases[k];
    read_summary(k);
  }
  resolve();
  // The helpers go after every object's code, then init.
  init_off = obj_code[n];
  for (i = 0; i < NHELPERS; i++)
    if (h_def[i] != NO_OBJ) {
      h_off[i] = init_off;
      init_off += h_size[i];
      if (opt_xload)
        add_def(h_off[i]);
    }
  if (opt_xload) {
    if (main_sym < 0)
      fatal0(268 /* "an XLOAD image needs main" */);
    if (nimports > 127)
      fatal0(273 /* "too many imports for an XLOAD image (127)" */);
    add_def(init_off);
  }

  // The dependencies: CMDSYS, if anything is imported, then each
  // module a used import comes from: their names, then $00.
  deplen = nimports ? (int)sizeof(dep) - 1 + 1 : 1;
  for (i = 0; i < nmods; i++)
    if (mod_used[i] && strcmp(mod_name[i], dep))
      deplen += strlen(mod_name[i]) > 16 ? 16 : (int)strlen(mod_name[i]);
  // The data segment holds the globals, then the string pool. One pad
  // byte between it and the bytecode keeps data from ever sharing an
  // address with the first function.
  data_base = opt_xload ? 0 : MODULE_BASE + hdr + deplen;
  strs_base = data_base + obj_data[n] + zstay + 2 * nmoved;
  subseg = strs_base + obj_strs[n] + 1;
  total = init_off + init_size();
  if (subseg + total > 0xFFFF)
    fatal0(191 /* "module too large" */);

  // Step 2.
  if (!bopen(&out, path, 1))
    fatal_d(192 /* "cannot create output file" */, path);
  write_header(dep, init_off, total);

  for (k = 0; k < n; k++)
    copy_data(k);
  for (i = zstay + 2 * nmoved; i > 0; i--)
    bputc(&out, 0);                       // zero-filled; pointer words
  for (k = 0; k < n; k++)
    copy_strings(k);
  bputc(&out, 0);                         // pad

  // Step 3.
  for (k = 0; k < n; k++)
    copy_code(k);
  copy_helpers();
  if (main_sym >= 0) {
    int len = init_code();
#ifdef CCISH_LISTING
    list_init(len, init_off);
#endif
    bwrite(&out, fbuf, len);
  }

  // Step 4.
  write_relocs(n, init_off);

  if (bclose(&out) != 0)
    fatal_d(193 /* "error writing output file" */, path);
#ifdef CCISH_LISTING
  list_summary(dep);
#endif
}

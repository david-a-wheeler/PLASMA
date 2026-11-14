#!/bin/sh
# The linker checks: cclink names a file it can't open, finds a damaged
# stream file or summary, and the linker's tables and one-offs (the
# shared-name table full, more functions than one object, many
# relocations, a static name that doesn't fit, two objects whose helpers
# differ, a list file of objects, and the usage for several objects
# without -o).
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT/link"

# cclink names the file it can't open (not another in the same static
# buffer: the message is printed after the stream files are removed).
cc_obj "" "$OUT/noobj" tests/run/hello.c
rm -f "$OUT/noobj.obj"
if ./cclink "$OUT/noobj" 2>"$OUT/noobj.err"; then
    echo "FAIL cclink: succeeded without its stream file"; bad=1
elif grep -qF "cannot open $OUT/noobj.obj" "$OUT/noobj.err"; then
    :
else
    echo "FAIL cclink: wanted 'cannot open $OUT/noobj.obj', got:"
    sed 's/^/    /' "$OUT/noobj.err"; bad=1
fi
rm -f "$OUT/noobj.syms"

# cclink finds a damaged stream file: here, one cut short, and one with an
# unknown record tag added at the end.
damaged_test() {
    if ./cclink "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL cclink: accepted a stream file $2"; bad=1
    elif grep -qE "damaged|unexpected end" "$OUT/$1.err"; then
        :
    else
        echo "FAIL cclink: wrong message for a stream file $2:"
        sed 's/^/    /' "$OUT/$1.err"; bad=1
    fi
    rm -f "$OUT/$1.obj" "$OUT/$1.syms"
}
cc_obj "" "$OUT/cut" tests/run/global_init.c
size=$(wc -c < "$OUT/cut.obj")
head -c $((size - 3)) "$OUT/cut.obj" > "$OUT/cut.tmp"
mv "$OUT/cut.tmp" "$OUT/cut.obj"
damaged_test cut "cut short"
cc_obj "" "$OUT/badtag" tests/run/global_init.c
printf 'X' >> "$OUT/badtag.obj"
damaged_test badtag "with an unknown record tag"

# A summary whose function starts at the code's end, where no function
# can, is found damaged as it is read: the message names the summary, not
# the stream file. The only symbol is main; its offset is bytes 20-21,
# after the 17-byte header and the module count (0). It becomes the
# code size, bytes 7-8.
echo 'int main() { return 0; }' >"$OUT/atend.c"
cc_obj "" "$OUT/atend" "$OUT/atend.c"
dd if="$OUT/atend.syms" of="$OUT/atend.size" bs=1 skip=7 count=2 2>/dev/null
dd if="$OUT/atend.size" of="$OUT/atend.syms" bs=1 seek=20 conv=notrunc \
   2>/dev/null
rm -f "$OUT/atend.size"
if ./cclink "$OUT/atend" 2>"$OUT/atend.err"; then
    echo "FAIL cclink: accepted a summary with a function at the code's end"; bad=1
elif grep -qF "damaged: $OUT/atend.syms" "$OUT/atend.err"; then
    :
else
    echo "FAIL cclink: wanted the summary damaged, for a function at the end:"
    sed 's/^/    /' "$OUT/atend.err"; bad=1
fi
rm -f "$OUT/atend.obj" "$OUT/atend.syms"

# The shared-name table full (approach.md issue 11, "Fitting the
# linker's tables"): 307 names fill all 307 slots and link, and b's use
# of a's g0 is found in the full table; one more name stops the link.
gen_decls() {
    i=$2
    while [ "$i" -le "$3" ]; do
        echo "void g$i(void);"
        i=$((i + 1))
    done >"$1"
}
gen_decls "$OUT/link/full_a.c" 1 153
echo 'void g0(void) {}' >>"$OUT/link/full_a.c"
gen_decls "$OUT/link/full_b.c" 154 305
echo 'void g0(void); void use(void) { g0(); }' >>"$OUT/link/full_b.c"
gen_decls "$OUT/link/full_c.c" 154 307
if cc_obj "" "$OUT/link/full_a" "$OUT/link/full_a.c" \
   && cc_obj "" "$OUT/link/full_b" "$OUT/link/full_b.c" \
   && ./cclink -v -o "$OUT/link/full" "$OUT/link/full_a" "$OUT/link/full_b" \
      >"$OUT/link/full.lst" 2>"$OUT/link/full.err" \
   && grep -qF "; shared names: 307 of 307 slots" "$OUT/link/full.lst"; then
    :
else
    echo "FAIL cclink: 307 shared names don't fill the table and link"
    sed 's/^/    /' "$OUT/link/full.err"; bad=1
fi
if cc_obj "" "$OUT/link/full_c" "$OUT/link/full_c.c" \
   && ! ./cclink -o "$OUT/link/full_over" "$OUT/link/full_a" \
        "$OUT/link/full_c" 2>"$OUT/link/full_over.err" \
   && grep -qF "too many names shared between objects" \
        "$OUT/link/full_over.err"; then
    :
else
    echo "FAIL cclink: 308 shared names, wanted 'too many names', got:"
    sed 's/^/    /' "$OUT/link/full_over.err"; bad=1
fi
rm -f "$OUT"/link/full_[abc].obj "$OUT"/link/full_[abc].syms

# More functions in a module than one object may define (MAX_FUNCS, 280):
# many_a defines f1..f160, many_b f161..f320, all static so they don't
# fill the shared-name table, and each object shares only two wrappers.
gen_funcs() {
    i=$2
    while [ "$i" -le "$3" ]; do
        echo "static int f$i(void) { return $i; }"
        i=$((i + 1))
    done >"$1"
    echo "int $4_first(void) { return f$2(); }" >>"$1"
    echo "int $4_last(void) { return f$3(); }" >>"$1"
}
gen_funcs "$OUT/link/many_a.c" 1 160 a
gen_funcs "$OUT/link/many_b.c" 161 320 b
cat >>"$OUT/link/many_b.c" <<'EOF'
int a_first(void); int a_last(void);
int main()
{
  __puti(a_first()); __putc(' '); __puti(a_last()); __putc(' ');
  __puti(b_first()); __putc(' '); __puti(b_last()); __putc(10);
  return 0;
}
EOF
if cc_obj "" "$OUT/link/many_a" "$OUT/link/many_a.c" \
   && cc_obj "" "$OUT/link/many_b" "$OUT/link/many_b.c" \
   && ./cclink -o "$OUT/link/many" "$OUT/link/many_a" "$OUT/link/many_b" \
      2>"$OUT/link/many.err" \
   && out=$(run_module "$OUT/link" many) \
   && [ "$out" = "1 160 161 320" ]; then
    :
else
    echo "FAIL cclink: 324 functions in two objects don't link and run"
    sed 's/^/    /' "$OUT/link/many.err"; bad=1
fi

# A list file (@file) names the same objects: one per line, with CR or
# LF ending a line, blank lines skipped, and no newline needed at the
# end; the module is the same as the command line's.
printf '\r\n%s\r\n\n%s' "$OUT/link/many_a" "$OUT/link/many_b" \
    >"$OUT/link/many.objs"
if ./cclink -o "$OUT/link/many_lst" "@$OUT/link/many.objs" \
       2>"$OUT/link/many_lst.err" \
   && cmp -s "$OUT/link/many" "$OUT/link/many_lst"; then
    :
else
    echo "FAIL cclink: a list file's link differs from the command line's"
    sed 's/^/    /' "$OUT/link/many_lst.err"; bad=1
fi
rm -f "$OUT"/link/many_[ab].obj "$OUT"/link/many_[ab].syms \
    "$OUT/link/many_lst" "$OUT/link/many.objs"

# A list file that can't be opened, one whose names overflow cclink's
# 512-byte buffer, and one that names more than 32 objects.
want_fail() {   # want_fail what message args...
    what=$1 want=$2; shift 2
    if ./cclink "$@" 2>"$OUT/link/list.err"; then
        echo "FAIL cclink: accepted $what"; bad=1
    elif grep -qF "$want" "$OUT/link/list.err"; then
        :
    else
        echo "FAIL cclink: wanted '$want' for $what, got:"
        sed 's/^/    /' "$OUT/link/list.err"; bad=1
    fi
}
want_fail "a missing list file" "cannot open $OUT/link/nolist.objs" \
    -o "$OUT/link/x" "@$OUT/link/nolist.objs"
i=0; : >"$OUT/link/long.objs"
while [ $i -lt 20 ]; do
    echo "$OUT/link/a_rather_long_object_name_$i" >>"$OUT/link/long.objs"
    i=$((i + 1))
done
want_fail "a list file too long" "names in the list file too long" \
    -o "$OUT/link/x" "@$OUT/link/long.objs"
i=0; : >"$OUT/link/many33.objs"
while [ $i -lt 33 ]; do echo "o$i" >>"$OUT/link/many33.objs"; i=$((i + 1)); done
want_fail "33 objects" "too many objects to link" \
    -o "$OUT/link/x" "@$OUT/link/many33.objs"
rm -f "$OUT/link/long.objs" "$OUT/link/many33.objs" "$OUT/link/list.err"

# More relocations than cclink once kept in memory (2432; B4 rereads them
# instead): f1..f25 each store to the global g 100 times, one data
# relocation each, 2500 in all.
{
    echo 'int g;'
    f=1
    while [ "$f" -le 25 ]; do
        echo "void f$f(void)"
        echo '{'
        i=1
        while [ "$i" -le 100 ]; do
            echo "  g = $((f * 1000 + i));"
            i=$((i + 1))
        done
        echo '}'
        f=$((f + 1))
    done
    echo 'int main()'
    echo '{'
    echo '  f1(); __puti(g); __putc(32); f25(); __puti(g); __putc(10);'
    echo '  return 0;'
    echo '}'
} >"$OUT/link/relocs.c"
if cc_obj "" "$OUT/link/relocs" "$OUT/link/relocs.c" \
      2>"$OUT/link/relocs.err" \
   && ./cclink "$OUT/link/relocs" 2>>"$OUT/link/relocs.err" \
   && out=$(run_module "$OUT/link" relocs) \
   && [ "$out" = "1100 25100" ]; then
    :
else
    echo "FAIL cclink: 2500 relocations don't link and run"
    sed 's/^/    /' "$OUT/link/relocs.err"; bad=1
fi
rm -f "$OUT"/link/relocs.obj "$OUT"/link/relocs.syms

# A static function's name that doesn't fit after the shared names is
# listed by its address (approach.md issue 11, "Item 1"). The module
# still links and runs.
gen_long() {
    i=$2
    while [ "$i" -le "$3" ]; do
        printf 'void n%030d(void);\n' "$i"
        i=$((i + 1))
    done >"$1"
}
# The 92 names of 31 characters, main, the 27-character caller, and
# __puti and __putc take 2991 of the 3000 name bytes, too few for the
# static name's 32.
gen_long "$OUT/link/nofit_a.c" 1 46
gen_long "$OUT/link/nofit_b.c" 47 92
cat >>"$OUT/link/nofit_b.c" <<'EOF'
static int static_padded_to_thirty_one_chr(void) { return 7; }
int caller_padded_to_27_chars_x(void)
{
  return static_padded_to_thirty_one_chr();
}
int main()
{
  __puti(caller_padded_to_27_chars_x()); __putc(10);
  return 0;
}
EOF
if cc_obj "" "$OUT/link/nofit_a" "$OUT/link/nofit_a.c" \
      2>"$OUT/link/nofit.err" \
   && cc_obj "" "$OUT/link/nofit_b" "$OUT/link/nofit_b.c" \
      2>>"$OUT/link/nofit.err" \
   && ./cclink -v -o "$OUT/link/nofit" "$OUT/link/nofit_a" \
      "$OUT/link/nofit_b" >"$OUT/link/nofit.lst" 2>>"$OUT/link/nofit.err" \
   && grep -qF "; shared names: 96 of 307 slots, 2991 of 3000" \
        "$OUT/link/nofit.lst" \
   && grep -q '^static@\$[0-9A-F]\{4\}:$' "$OUT/link/nofit.lst" \
   && grep -qF '; static@$' "$OUT/link/nofit.lst" \
   && ! grep -qF "static_padded" "$OUT/link/nofit.lst" \
   && out=$(run_module "$OUT/link" nofit) \
   && [ "$out" = "7" ]; then
    :
else
    echo "FAIL cclink: a static name that doesn't fit isn't listed by address"
    sed 's/^/    /' "$OUT/link/nofit.err"; bad=1
fi
rm -f "$OUT"/link/nofit_[ab].obj "$OUT"/link/nofit_[ab].syms

# Two objects whose copies of a helper differ (compiled by different
# ccgens) are an error, not a silent merge: here one checksum changes.
# For udiv alone, the checksum's low byte is the 7th byte from the end.
cc_obj "" "$OUT/link/hm_a" tests/link/helpers/a.c
cc_obj "" "$OUT/link/hm_b" tests/link/helpers/b.c
size=$(wc -c < "$OUT/link/hm_b.syms")
b=$(od -An -tu1 -j $((size - 7)) -N1 "$OUT/link/hm_b.syms" | tr -d ' ')
printf "\\$(printf '%03o' $(( (b + 1) % 256 )))" \
    | dd of="$OUT/link/hm_b.syms" bs=1 seek=$((size - 7)) conv=notrunc \
         2>/dev/null
if ./cclink -o "$OUT/link/hm" "$OUT/link/hm_a" "$OUT/link/hm_b" \
       2>"$OUT/link/hm.err"; then
    echo "FAIL cclink: linked objects whose helpers differ"; bad=1
elif grep -qF "helper functions differ between objects" "$OUT/link/hm.err"
then
    :
else
    echo "FAIL cclink: wanted 'helper functions differ', got:"
    sed 's/^/    /' "$OUT/link/hm.err"; bad=1
fi
rm -f "$OUT"/link/hm_[ab].obj "$OUT"/link/hm_[ab].syms

# Linking several objects needs a module name.
if ./cclink "$OUT/a" "$OUT/b" 2>"$OUT/link/noname.err"; then
    echo "FAIL cclink: linked several objects without -o"; bad=1
elif grep -qF "usage: cclink" "$OUT/link/noname.err"; then
    :
else
    echo "FAIL cclink: wanted its usage for several objects without -o, got:"
    sed 's/^/    /' "$OUT/link/noname.err"; bad=1
fi

# ---------- -L: the library's objects (doc/linking.md, section 5) ----------
LB=$OUT/lib

# A program that needs objects two levels deep: fopen, then path
# (which it calls for the FILEIO name), which needs strlen. The
# listing names the objects added, and linking twice is the same
# module (the passes in order, the map in file order).
cat >"$OUT/link/lmap.c" <<'EOF'
#include <stdio.h>
int main(void)
{
  FILE *f = fopen("lmap.dat", "w");
  int ok = f != 0;
  if (f)
    fclose(f);
  __puti(ok);
  __putln();
  return ok ? 0 : 1;
}
EOF
if cc_obj "" "$OUT/link/lmap" "$OUT/link/lmap.c" \
   && ./cclink -v -L "$LB" -o "$OUT/link/lmap" "$OUT/link/lmap" \
      >"$OUT/link/lmap.lst" 2>"$OUT/link/lmap.err" \
   && out=$(run_module "$OUT/link" lmap) \
   && [ "$out" = "1" ] \
   && grep -qF "; library object: fopen" "$OUT/link/lmap.lst" \
   && grep -qF "; library object: path" "$OUT/link/lmap.lst" \
   && grep -qF "; library object: strlen" "$OUT/link/lmap.lst" \
   && ./cclink -L "$LB" -o "$OUT/link/lmap2" "$OUT/link/lmap" \
      2>>"$OUT/link/lmap.err" \
   && cmp -s "$OUT/link/lmap" "$OUT/link/lmap2"; then
    :
else
    echo "FAIL cclink: -L didn't add the library's objects (fopen, path, strlen)"
    sed 's/^/    /' "$OUT/link/lmap.err"; bad=1
fi
rm -f "$OUT/link/lmap2" "$OUT/link/lmap.dat"

# The driver's -L does the same, compiling and linking in one.
if ./ccish -L "$LB" -o "$OUT/link/lmapd" "$OUT/link/lmap.c" \
      2>"$OUT/link/lmapd.err" \
   && out=$(run_module "$OUT/link" lmapd) \
   && [ "$out" = "1" ]; then
    :
else
    echo "FAIL cclink: ccish -L didn't compile and link"
    sed 's/^/    /' "$OUT/link/lmapd.err"; bad=1
fi
rm -f "$OUT/link/lmapd" "$OUT/link/lmap.dat"

# One line naming two names the link wants (the one object defines
# both), with a third name in between: the line is judged at the
# first, and the rest of it is skipped, not read as a new line's base
# (which would add the name as an object).
cat >"$OUT/link/mw.c" <<'EOF'
int mw_a(void) { return 1; }
int mw_b(void) { return 2; }
int mw_c(void) { return 3; }
EOF
cat >"$OUT/link/mwp.c" <<'EOF'
int mw_a(void); int mw_c(void);
int main(void)
{
  __puti(mw_a() + mw_c()); __putln();
  return 0;
}
EOF
if cc_obj "" "$OUT/link/mw" "$OUT/link/mw.c" \
   && cc_obj "" "$OUT/link/mwp" "$OUT/link/mwp.c" \
   && mkdir -p "$OUT/link/mwlib" \
   && cp "$OUT/link/mw.obj" "$OUT/link/mw.syms" "$OUT/link/mwlib/" \
   && echo "mw mw_a mw_b mw_c" > "$OUT/link/mwlib/lib.map" \
   && ./cclink -L "$OUT/link/mwlib" -o "$OUT/link/mwp" "$OUT/link/mwp" \
      2>"$OUT/link/mwp.err" \
   && out=$(run_module "$OUT/link" mwp) \
   && [ "$out" = "4" ]; then
    :
else
    echo "FAIL cclink: a line with two wanted names added a second object"
    sed 's/^/    /' "$OUT/link/mwp.err"; bad=1
fi
rm -f "$OUT"/link/mw "$OUT"/link/mw.c "$OUT"/link/mw.obj \
      "$OUT"/link/mw.syms "$OUT"/link/mwp "$OUT"/link/mwp.c \
      "$OUT"/link/mwp.err
rm -rf "$OUT"/link/mwlib

# -L with no map: the 171 for the map's path, before any object is
# read (the named object is missing, too: the map's error wins).
want_fail "a missing map" "cannot open $OUT/link/nomap/lib.map" \
    -L "$OUT/link/nomap" -o "$OUT/link/x" "$OUT/link/nomissing"

# A map naming an object that isn't there: the 171 for its summary.
mkdir -p "$OUT/link/lmisslib"
echo 'int libmiss(void); int main(void) { __puti(libmiss()); __putln(); return 0; }' \
    >"$OUT/link/lmiss.c"
cc_obj "" "$OUT/link/lmiss" "$OUT/link/lmiss.c"
echo "nosuch libmiss" > "$OUT/link/lmisslib/lib.map"
want_fail "a map naming a missing object" \
    "cannot open $OUT/link/lmisslib/nosuch.syms" \
    -L "$OUT/link/lmisslib" -o "$OUT/link/x" "$OUT/link/lmiss"

# A map saying an object defines a name it doesn't: the drift error,
# naming both. First, for an object already named:
echo 'int driftname(void); int main(void) { __puti(driftname()); __putln(); return 0; }' \
    >"$OUT/link/drift.c"
cc_obj "" "$OUT/link/drift" "$OUT/link/drift.c"
mkdir -p "$OUT/link/driftlib"
echo "drift driftname" > "$OUT/link/driftlib/lib.map"
want_fail "a map line for a named object that doesn't define the name" \
    "the library map says 'drift' defines 'driftname', but it doesn't" \
    -L "$OUT/link/driftlib" -o "$OUT/link/x" "$OUT/link/drift"

# And, for one the map itself added: it defines a wanted name on its
# line, and claims another it doesn't have. The pass after its summary
# finds the name still wanted and the base already linked.
echo 'int driftother(void); int driftmiss(void);
int main(void)
{
  __puti(driftother()); __puti(driftmiss()); __putln();
  return 0;
}' >"$OUT/link/drift2.c"
cc_obj "" "$OUT/link/drift2" "$OUT/link/drift2.c"
mkdir -p "$OUT/link/driftlib2"
echo 'int driftother(void) { return 3; }' >"$OUT/link/driftlib2/other.c"
cc_obj "" "$OUT/link/driftlib2/other" "$OUT/link/driftlib2/other.c"
echo "other driftother driftmiss" > "$OUT/link/driftlib2/lib.map"
want_fail "a map line for an added object that doesn't define the name" \
    "the library map says 'other' defines 'driftmiss', but it doesn't" \
    -L "$OUT/link/driftlib2" -o "$OUT/link/x" "$OUT/link/drift2"

# A name on no line of the real map: the 172 text, as without -L.
echo 'int notinthemap(void); int main(void) { __puti(notinthemap()); __putln(); return 0; }' \
    >"$OUT/link/notline.c"
cc_obj "" "$OUT/link/notline" "$OUT/link/notline.c"
want_fail "a name on no line" "undefined reference to 'notinthemap'" \
    -L "$LB" -o "$OUT/link/x" "$OUT/link/notline"

# libmap.py fails if two objects define the same name, so a map can't
# be ambiguous.
mkdir -p "$OUT/link/mapdup"
cp "$LB/exit.syms" "$OUT/link/mapdup/a.syms"
cp "$LB/exit.syms" "$OUT/link/mapdup/b.syms"
if python3 tools/libmap.py "$OUT/link/mapdup" 2>"$OUT/link/mapdup.err"; then
    echo "FAIL libmap: two objects defining one name made a map"; bad=1
elif grep -qF "two objects define exit" "$OUT/link/mapdup.err"; then
    :
else
    echo "FAIL libmap: wrong message for two objects, one name:"
    sed 's/^/    /' "$OUT/link/mapdup.err"; bad=1
fi
rm -rf "$OUT/link/mapdup"
rm -f "$OUT/link/lmiss.obj" "$OUT/link/lmiss.syms" \
    "$OUT/link/drift.obj" "$OUT/link/drift.syms" \
    "$OUT/link/drift2.obj" "$OUT/link/drift2.syms" \
    "$OUT/link/notline.obj" "$OUT/link/notline.syms" \
    "$OUT/link/driftlib2/other.obj" "$OUT/link/driftlib2/other.syms"
rm -rf "$OUT/link/lmisslib" "$OUT/link/driftlib" "$OUT/link/driftlib2"
exit $bad

#!/bin/sh
# The linker checks: ccrel names a file it can't open, finds a damaged
# stream file or summary, and the linker's tables and one-offs (the
# shared-name table full, more functions than one object, many
# relocations, a static name that doesn't fit, two objects whose helpers
# differ, a list file of objects, and the usage for several objects
# without -o).
. "$(dirname "$0")/../lib.sh"
OUT=tests/out
bad=0
mkdir -p "$OUT/link"

# ccrel names the file it can't open (not another in the same static
# buffer: the message is printed after the stream files are removed).
cc_obj "" "$OUT/noobj" tests/run/hello.c
rm -f "$OUT/noobj.obj"
if ./ccrel "$OUT/noobj" 2>"$OUT/noobj.err"; then
    echo "FAIL ccrel: succeeded without its stream file"; bad=1
elif grep -qF "cannot open $OUT/noobj.obj" "$OUT/noobj.err"; then
    :
else
    echo "FAIL ccrel: wanted 'cannot open $OUT/noobj.obj', got:"
    sed 's/^/    /' "$OUT/noobj.err"; bad=1
fi
rm -f "$OUT/noobj.syms"

# ccrel finds a damaged stream file: here, one cut short, and one with an
# unknown record tag added at the end.
damaged_test() {
    if ./ccrel "$OUT/$1" 2>"$OUT/$1.err"; then
        echo "FAIL ccrel: accepted a stream file $2"; bad=1
    elif grep -qE "damaged|unexpected end" "$OUT/$1.err"; then
        :
    else
        echo "FAIL ccrel: wrong message for a stream file $2:"
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
# the stream file. The only symbol is main; its offset is bytes 18-19,
# after the 15-byte header and the module count (0). It becomes the
# code size, bytes 7-8.
echo 'int main() { return 0; }' >"$OUT/atend.c"
cc_obj "" "$OUT/atend" "$OUT/atend.c"
dd if="$OUT/atend.syms" of="$OUT/atend.size" bs=1 skip=7 count=2 2>/dev/null
dd if="$OUT/atend.size" of="$OUT/atend.syms" bs=1 seek=18 conv=notrunc \
   2>/dev/null
rm -f "$OUT/atend.size"
if ./ccrel "$OUT/atend" 2>"$OUT/atend.err"; then
    echo "FAIL ccrel: accepted a summary with a function at the code's end"; bad=1
elif grep -qF "damaged: $OUT/atend.syms" "$OUT/atend.err"; then
    :
else
    echo "FAIL ccrel: wanted the summary damaged, for a function at the end:"
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
   && ./ccrel -l -o "$OUT/link/full" "$OUT/link/full_a" "$OUT/link/full_b" \
      >"$OUT/link/full.lst" 2>"$OUT/link/full.err" \
   && grep -qF "; shared names: 307 of 307 slots" "$OUT/link/full.lst"; then
    :
else
    echo "FAIL ccrel: 307 shared names don't fill the table and link"
    sed 's/^/    /' "$OUT/link/full.err"; bad=1
fi
if cc_obj "" "$OUT/link/full_c" "$OUT/link/full_c.c" \
   && ! ./ccrel -o "$OUT/link/full_over" "$OUT/link/full_a" \
        "$OUT/link/full_c" 2>"$OUT/link/full_over.err" \
   && grep -qF "too many names shared between objects" \
        "$OUT/link/full_over.err"; then
    :
else
    echo "FAIL ccrel: 308 shared names, wanted 'too many names', got:"
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
   && ./ccrel -o "$OUT/link/many" "$OUT/link/many_a" "$OUT/link/many_b" \
      2>"$OUT/link/many.err" \
   && out=$(run_module "$OUT/link" many) \
   && [ "$out" = "1 160 161 320" ]; then
    :
else
    echo "FAIL ccrel: 324 functions in two objects don't link and run"
    sed 's/^/    /' "$OUT/link/many.err"; bad=1
fi

# A list file (@file) names the same objects: one per line, with CR or
# LF ending a line, blank lines skipped, and no newline needed at the
# end; the module is the same as the command line's.
printf '\r\n%s\r\n\n%s' "$OUT/link/many_a" "$OUT/link/many_b" \
    >"$OUT/link/many.objs"
if ./ccrel -o "$OUT/link/many_lst" "@$OUT/link/many.objs" \
       2>"$OUT/link/many_lst.err" \
   && cmp -s "$OUT/link/many" "$OUT/link/many_lst"; then
    :
else
    echo "FAIL ccrel: a list file's link differs from the command line's"
    sed 's/^/    /' "$OUT/link/many_lst.err"; bad=1
fi
rm -f "$OUT"/link/many_[ab].obj "$OUT"/link/many_[ab].syms \
    "$OUT/link/many_lst" "$OUT/link/many.objs"

# A list file that can't be opened, one whose names overflow ccrel's
# 512-byte buffer, and one that names more than 32 objects.
want_fail() {   # want_fail what message args...
    what=$1 want=$2; shift 2
    if ./ccrel "$@" 2>"$OUT/link/list.err"; then
        echo "FAIL ccrel: accepted $what"; bad=1
    elif grep -qF "$want" "$OUT/link/list.err"; then
        :
    else
        echo "FAIL ccrel: wanted '$want' for $what, got:"
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

# More relocations than ccrel once kept in memory (2432; B4 rereads them
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
   && ./ccrel "$OUT/link/relocs" 2>>"$OUT/link/relocs.err" \
   && out=$(run_module "$OUT/link" relocs) \
   && [ "$out" = "1100 25100" ]; then
    :
else
    echo "FAIL ccrel: 2500 relocations don't link and run"
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
   && ./ccrel -l -o "$OUT/link/nofit" "$OUT/link/nofit_a" \
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
    echo "FAIL ccrel: a static name that doesn't fit isn't listed by address"
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
if ./ccrel -o "$OUT/link/hm" "$OUT/link/hm_a" "$OUT/link/hm_b" \
       2>"$OUT/link/hm.err"; then
    echo "FAIL ccrel: linked objects whose helpers differ"; bad=1
elif grep -qF "helper functions differ between objects" "$OUT/link/hm.err"
then
    :
else
    echo "FAIL ccrel: wanted 'helper functions differ', got:"
    sed 's/^/    /' "$OUT/link/hm.err"; bad=1
fi
rm -f "$OUT"/link/hm_[ab].obj "$OUT"/link/hm_[ab].syms

# Linking several objects needs a module name.
if ./ccrel "$OUT/a" "$OUT/b" 2>"$OUT/link/noname.err"; then
    echo "FAIL ccrel: linked several objects without -o"; bad=1
elif grep -qF "usage: ccrel" "$OUT/link/noname.err"; then
    :
else
    echo "FAIL ccrel: wanted its usage for several objects without -o, got:"
    sed 's/^/    /' "$OUT/link/noname.err"; bad=1
fi
exit $bad

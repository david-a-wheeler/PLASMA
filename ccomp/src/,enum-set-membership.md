# Improving enum subset handling

We have some larger enums in the C compiler code (ccomp/) in which there
are important subsets. Size and speed are important for this compiler.

We should order the enums defined in various .h files, and add #defines in
the .h files to implement efficient and clear set-membership checks within
the appropriate .h files.  We should then use them where appropriate
(especially in parse.c).

For example, in parse.c we have:

~~~~
/* Does the current token start a type name: a type keyword, a
 * qualifier, or a visible typedef name? */
static int is_type_start(void)
{
  return tok == T_INT || tok == T_CHAR || tok == T_VOID || tok == T_CONST
         || tok == T_VOLATILE || tok == T_STRUCT || tok == T_ENUM
         || tok == T_SIGNED || tok == T_UNSIGNED
         || (tok == T_ID && typedef_type(tok_val) >= 0);
}
~~~~

If we carefully ordered the enum values in `tokin.h` we could implement
most or all of this with a range check within `tokin.h`, which would then
be used instead. E.g., in `tokin.h` we'd have:

~~~~
#define SIMPLE_TYPE_START(tok) ((tok) >= T_INT && (tok) <= T_UNSIGNED)
~~~~

Once this definition exists, is_type_start would instead be:

~~~~
static int is_type_start(void)
{
  return SIMPLE_TYPE_START(tok)
         || (tok == T_ID && typedef_type(tok_val) >= 0);
~~~~

This change would be better for maintainability *and* efficiency.
In this approach, the .c file wouldn't need to know about the exact
ordering of the type start tokens (that would stay concentrated in
tokin.h), but the function would be much faster and shorter.
Adding a new member of the set would be easier; the new member could
be added "at the right place" and the #defines adjusted if necessary;
many .c lines would not need to be changed at all.

Similar problems happen in function parse_int_type and parse_base_type.

You can use this command to find plausible cases:

~~~~
grep -E '[A-Z][A-Z].*\|\|.*[A-Z][A-Z].*\|\|.*[A-Z][A-Z]' *.c
~~~~

As a first step, please run this command to find plausible cases, look at
their context, and develop a set of proposed groupings and their names.
Then look at the corresponding .h files where those enums are defined.
Suggest how they can be modified so that related tokens are
next to each other (so we can use ranges to quickly identify the
subset), and what #defines we should add to the .h files to
efficiently compute those subsets.

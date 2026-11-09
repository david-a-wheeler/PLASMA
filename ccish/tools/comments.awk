# ,comments.awk -- convert /* ... */ comments to // comments.
#
# One file per run; the converted source goes to stdout, a short
# report to stderr:
#
#   awk -f ,comments.awk src/pexpr.c > ,pexpr.c
#
# All of src/ and tests/ at once, in place (sponge), from the ccish
# directory. The four excluded files test comment shapes (two-line
# block comments without * bullets, an unterminated /*, and comments
# in odd places); their shapes are the tests, so they stay unconverted:
#
#   find src tests -type f \( -name '*.c' -o -name '*.h' \) \
#       ! -path tests/errors/line_after_comment.c \
#       ! -path tests/errors/unterminated_comment.c \
#       ! -path tests/pp/tokens.c \
#       ! -path tests/run/digraphs.c \
#       -exec sh -c 'awk -f ,comments.awk "$1" | sponge "$1"' _ {} \;
#
# Converted:
#   /* text */            a whole line       -> // text
#   code /* text */       comment at EOL     -> code // text
#   /* first line         a block comment    -> // first line
#    * other line                                     // other line
#    */                                                 the // lines are
#                                                      aligned under the
#                                                      /*, a first line
#                                                      that is a lone /*
#                                                      is dropped, the *
#                                                      bullets go (their
#                                                      one space too,
#                                                      other indents
#                                                      kept), and a
#                                                      closing line with
#                                                      no text is dropped
#
#   code /* first line    a block comment    -> code // first line
#      * other line          opened after the       the other // lines
#      */                            code                 are under the /*
#
# Left alone, unchanged:
#   - a comment with code after its closing */ (the
#     error(4 /* "text" */, ...) message comments);
#   - a comment whose text is a quoted string, the message texts of
#     the error(4 /* "text" */, ...) convention, in whatever shape
#     (one line, wrapped, or closing at the end of the line); and
#   - a multi-line comment with any line, including its closing
#     line, that has no * bullet (the wrapped message texts).
#
# A comment not closed at end of file is left alone, with a note on
# stderr.

function rstrip(s) { sub(/[ \t]+$/, "", s); return s }
function lstrip(s) { sub(/^[ \t]+/, "", s); return s }

function report() {
  if (conv + left > 0)
    printf(",comments: %s: %d converted, %d left alone\n",
           prev, conv, left) > "/dev/stderr"
}

# The buffered lines of a comment left alone (or not closed at end of
# file) go back unchanged.
function unbuf() {
  for (i = 1; i <= bn; i++)
    print buf[i]
  bn = 0
}

# A comment not closed at end of file goes back unchanged, with a
# note; at each new file and at end.
function flush_unclosed() {
  if (inc) {
    unbuf()
    printf(",comments: %s: unclosed comment left alone\n",
           prev) > "/dev/stderr"
    inc = 0
  }
}

# Spaces as wide as everything before the /* of a comment opened
# after code, so that the comment's // lines go under the /* (a tab
# counts to the next tab stop of 8).
function under_open(s,    i, c, w, out) {
  w = 0
  for (i = 1; i <= length(s); i++) {
    c = substr(s, i, 1)
    if (c == "\t")
      w = (int(w / 8) + 1) * 8
    else
      w++
  }
  out = ""
  while (w-- > 0)
    out = out " "
  return out
}

# The first line of a comment: whitespace or code, /*, text that ends
# at end (the line's end, or the closing */ on a one-line comment).
# Sets c_ws, the // column for the whole comment: as many spaces as
# everything before the /*, so a comment opened after code has its
# // lines under the /*. A lone /* makes no line at all (a // with
# no text conveys nothing).
function conv_open(line, end,    p, before, text) {
  p = index(line, "/*")
  before = substr(line, 1, p - 1)
  text = rstrip(lstrip(substr(line, p + 2, end - p - 2)))
  c_ws = under_open(before)
  if (before !~ /^[ \t]*$/)
    return text == "" ? rstrip(before) : \
           (before ~ /[ \t]$/ ? before : before " ") "// " text
  return text == "" ? "" : c_ws "// " text
}

# A continuation line: its bullet, the * and its space, becomes // at
# the // column; the rest of the line, its indents included, is kept.
function conv_cont(line,    rest) {
  match(line, /^[ \t]*\* ?/)
  rest = substr(line, RSTART + RLENGTH)
  if (rest ~ /^[ \t]*$/)
    return c_ws "//"
  return c_ws "// " rest
}

# The part of the closing line before its */: the bullet goes, the
# indents are kept; "" drops the line.
function conv_close(head,    rest) {
  if (match(head, /^[ \t]*\* ?/))
    rest = substr(head, RSTART + RLENGTH)
  else
    rest = lstrip(head)
  rest = rstrip(rest)
  return rest == "" ? "" : c_ws "// " rest
}

FNR == 1 {
  if (NR > 1)
    report()
  flush_unclosed()
  prev = FILENAME; conv = 0; left = 0
}

{
  line = $0
  if (inc) {
    p = index(line, "*/")
    if (p == 0) {
      buf[++bn] = line
      if (line !~ /^[ \t]*\*/)
        bad = 1
      next
    }
    code_after = (substr(line, p + 2) !~ /^[ \t]*$/)
    if (code_after || bad || quoted || line !~ /^[ \t]*\*/) {
      unbuf()
      print line
      left++
    } else {
      first = conv_open(buf[1], length(buf[1]) + 1)
      if (first != "")
        print first
      for (i = 2; i <= bn; i++)
        print conv_cont(buf[i])
      cl = conv_close(substr(line, 1, p - 1))
      if (cl != "")
        print cl
      conv++
    }
    inc = 0; bn = 0; bad = 0; quoted = 0
    next
  }
  p = index(line, "/*")
  if (p == 0) {
    print line
    next
  }
  rest = substr(line, p + 2)
  q = index(rest, "*/")
  if (q == 0) {
    inc = 1; bad = 0; bn = 1
    buf[1] = line
    quoted = (lstrip(rest) ~ /^"/)
    next
  }
  before = substr(line, 1, p - 1)
  text = rstrip(lstrip(substr(rest, 1, q - 1)))
  if (substr(rest, q + 2) !~ /^[ \t]*$/ || text ~ /^"/) {
    print line
    left++
    next
  }
  first = conv_open(line, p + q + 1)
  if (first != "")
    print first
  if (text != "")
    conv++
}

END { flush_unclosed(); report() }

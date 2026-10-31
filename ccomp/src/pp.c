/*
 * ccpp's driver (approach.md, "ccpp design"): reads the source's tokens
 * from the lexer and writes them to the token stream. Directives are
 * not supported yet (steps 4-6).
 */
#include "ccish.h"
#include "token.h"
#include "ppname.h"
#include "pplex.h"
#include "ppout.h"
#include "pp.h"

static Bfile src_file;

/* Preprocess source file src into the token stream and name file of
 * output name base. */
void pp_file(const char *src, const char *base)
{
  int c;
  src_name = src;
  if (!bopen(&src_file, src, 0))
    fatal(171 /* "cannot open" */, src);
  pp_names_init();
  pp_add_file(src);
  pp_out_open(base);
  pp_lex_open(&src_file);
  for (;;) {
    c = pp_lex();
    if (c == TC_END)
      break;
    if (c == '#') {
      if (pt_first)
        error(13 /* "preprocessor directives are not supported yet" */,
              NULL);
      error(17 /* "unexpected character in source" */, NULL);
    }
    pp_out(c, pt_val, pt_text, pt_len);
  }
  bclose(&src_file);
  src_line = 0;
  pp_out_finish();
}

/* After an error: close the open files, so that ProDOS can remove the
 * outputs. */
void pp_abort(void)
{
  bclose(&src_file);
  pp_out_abort();
}

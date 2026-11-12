// ccpp's driver (approach.md, "ccpp design"): reads the source's tokens
// from the lexer (or a macro's expansion) and writes them to the token
// stream, and carries out the directives: #include, #define, #undef,
// #if, #ifdef, #ifndef, #elif, #else, #endif, #error and #line; #pragma
// is ignored.
//
// The groups #ifdef and the rest open are a stack (cond[]): each one's
// state is C_TAKING (its tokens are kept), C_WAIT (skipped, no branch
// taken yet), C_DONE (skipped, a branch was taken) or C_DEAD (skipped,
// since the group around it is), with C_ELSE once its #else is seen.
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "bio.h"
#include "token.h"
#include "fixed.h"
#include "ppname.h"
#include "pplex.h"
#include "ppmac.h"
#include "ppif.h"
#include "ppout.h"
#include "pp.h"
#ifndef CCISH_INC
#include <plasma.h>
#endif

#define MAX_IF_NEST  8      // limit pp: #if, #ifdef and #ifndef groups open
                            // at once
#define MAX_INC_DIRS 8      // limit pp: -I directories
#define MAX_PP_OPTS  16     // limit pp: -D and -U options

int pp_cur_file;

// The source and its includes, innermost last, each with its number in
// the name file, and how many groups were open when it began.
static Bfile files[MAX_INCLUDE + 1];
static int file_no[MAX_INCLUDE + 1];
static char file_conds[MAX_INCLUDE + 1];
static int depth;
static char cur_path[MAX_PATH];     // the current file's name
#ifdef CCISH_STATS
static int depth_peak, conds_peak;  // the most of each at once
#endif

enum { C_TAKING, C_WAIT, C_DONE, C_DEAD, C_STATE = 3, C_ELSE = 4 };
static char cond[MAX_IF_NEST];
static int nconds;

// The command line's options, in order: the directories to look for
// includes in, and the -D and -U options (each "D" or "U" then its
// text).
static const char *inc_dirs[MAX_INC_DIRS];
static int ninc_dirs;
static const char *opt_text[MAX_PP_OPTS];
static char opt_kind[MAX_PP_OPTS];
static int nopts;

// Add -I dir: a directory to look for #include files in.
void pp_include_dir(const char *dir)
{
  if (ninc_dirs >= MAX_INC_DIRS)
    fatal0(248 /* "too many -I, -D and -U options" */);
  inc_dirs[ninc_dirs++] = dir;
}

// Add -D text or -U text (kind 'D' or 'U'), to apply in order.
void pp_option(int kind, const char *text)
{
  if (nopts >= MAX_PP_OPTS)
    fatal0(248 /* "too many -I, -D and -U options" */);
  opt_kind[nopts] = (char)kind;
  opt_text[nopts++] = text;
}

// If argv[i] is -I, -D or -U, with its value after it or in the next
// argument, record it and return the index of its last argument;
// otherwise return -1.
int pp_arg(int argc, char **argv, int i)
{
  const char *a = argv[i], *v = a + 2;
  if (a[0] != '-' || (a[1] != 'I' && a[1] != 'D' && a[1] != 'U'))
    return -1;
  if (!*v) {
    if (i + 1 >= argc)
      return -1;
    v = argv[++i];
  }
  if (a[1] == 'I')
    pp_include_dir(v);
  else
    pp_option(a[1], v);
  return i;
}

// Are the current tokens kept (not in a group being skipped)?
static int taking(void)
{
  return nconds == 0 || (cond[nconds - 1] & C_STATE) == C_TAKING;
}

// Set the lexer's mode for after a directive: skipping or not.
static void after_directive(void)
{
  pp_mode = taking() ? PM_TEXT : PM_SKIP;
}

// The rest of the directive's line must be empty. name, the
// directive's, is only for the message, which the small build prints
// without it (diag.h): there, a macro drops it, not evaluated, from
// each call, as here and in macro_name() and do_ifdef().
#ifdef CCISH_DIAG
static void end_directive(const char *name)
#else
#define end_directive(name) end_directive_()
static void end_directive_(void)
#endif
{
  if (pp_lex() != TC_NL)
    error(246 /* "extra tokens at the end of the directive:" */, name);
}

// #pragma. "#pragma plasma import NAME" names the PLASMA module to load
// for the externs declared after it in the same file (approach.md issue
// 22, decision 35): it becomes the tokens __plasma_import NAME, which
// cccomp reads. Any other pragma is ignored, as C allows.
static void do_pragma(void)
{
  char w[8];
  if (pp_lex_word(w, sizeof(w)) == 6 && !strcmp(w, "plasma")
      && pp_lex_word(w, sizeof(w)) == 6 && !strcmp(w, "import")) {
    if (pp_lex() != TC_ID)
      error0(13 /* "expected a module name after #pragma plasma"
                   " import" */);
    pp_out(TC_ID, ID_PIMPORT, NULL, 0);
    pp_out(TC_ID, pt_val, NULL, 0);
    end_directive("pragma");
    return;
  }
  while (pp_lex() != TC_NL)
    ;
}

// Read a macro's name, an identifier (or a keyword ccish doesn't
// support, which is one too), after directive name (for the message,
// as end_directive()'s); return its id.
#ifdef CCISH_DIAG
static int macro_name(const char *name)
#else
#define macro_name(name) macro_name_()
static int macro_name_(void)
#endif
{
  int c = pp_lex();
  if (c != TC_ID && c != TC_KEYWORD)
    error(238 /* "expected a macro name after" */, name);
  return pt_val;
}

// #define name body, an object-like macro, or #define name(params)
// body, a function-like one: a '(' right after the name, with no blank.
// The parameters are names, the last of which may be "...", which is
// __VA_ARGS__ in the body.
static void do_define(void)
{
  int id = macro_name("#define"), c, np = -1, va = 0, i;
  int params[MAX_PP_PARAMS];
  if (pp_lex_next_char() == '(') {
    pp_lex();                   // '('
    np = 0;
    c = pp_lex();
    while (c != ')') {
      if (np >= MAX_PP_PARAMS)
        fatal0(257 /* "too many macro parameters" */);
      if (c == TC_ELLIPSIS) {
        params[np++] = pp_intern("__VA_ARGS__", 11);
        va = 1;
        c = pp_lex();
        if (c != ')')
          error0(255 /* "bad macro parameter list" */);
        break;
      }
      if (c != TC_ID && c != TC_KEYWORD)
        error0(255 /* "bad macro parameter list" */);
      for (i = 0; i < np; i++)
        if (params[i] == pt_val)
          error0(255 /* "bad macro parameter list" */);
      params[np++] = pt_val;
      c = pp_lex();
      if (c == ',')
        c = pp_lex();
      else if (c != ')')
        error0(255 /* "bad macro parameter list" */);
    }
  }
  pp_def_begin(np, params, va);
  while ((c = pp_lex()) != TC_NL)
    pp_def_token(c, pt_val, pt_text, pt_len);
  pp_def_end(id);
}

// #undef name.
static void do_undef(void)
{
  pp_undef(macro_name("#undef"));
  end_directive("#undef");
}

// Try to open path for an #include, as the next file: 1 if it opened.
static int try_open(const char *path)
{
  if (!bopen(&files[depth + 1], path, 0))
    return 0;
  strcpy(cur_path, path);
  return 1;
}

// Join dir (the first n characters of it), a '/' unless n is 0, and
// name into buf (MAX_PATH bytes); 0 if it is too long.
static int join(char *buf, const char *dir, int n, const char *name)
{
  if (n + 1 + (int)strlen(name) >= MAX_PATH)
    return 0;
  memcpy(buf, dir, n);
  buf[n] = 0;
  if (n > 0)
    strcat(buf, "/");
  strcat(buf, name);
  return 1;
}

// #include "name" or <name>: "name" is looked for beside the file that
// includes it, then in the include directories (the -I ones, then the
// default); <name> only in those. The file is read next, from the end
// of the #include line.
static void do_include(void)
{
  static char name[MAX_PATH], buf[MAX_PATH];  // too big for a frame
  const char *slash;
  int angle, i, found = 0;
  if (pp_lex_header(name, MAX_PATH, &angle) < 0)
    error0(235 /* "expected \"file\" or <file> after #include" */);
  end_directive("#include");
  if (depth >= MAX_INCLUDE)
    error(237 /* "#include nested too deeply:" */, name);
  if (!angle) {
    slash = strrchr(cur_path, '/');
    found = join(buf, cur_path, slash ? (int)(slash - cur_path) : 0, name)
            && try_open(buf);
  }
  for (i = 0; !found && i < ninc_dirs; i++)
    found = join(buf, inc_dirs[i], (int)strlen(inc_dirs[i]), name)
            && try_open(buf);
  if (!found)
    error_d(236 /* "cannot open the #include file (missing, or too many "
                   "files open):" */, name);
  depth++;
#ifdef CCISH_STATS
  if (depth > depth_peak)
    depth_peak = depth;
#endif
  file_no[depth] = pp_add_file(cur_path);
  file_conds[depth] = (char)nconds;
  pp_cur_file = file_no[depth];
  src_name = cur_path;
  pp_mode = PM_TEXT;
  pp_lex_push(&files[depth]);
}

// Open a group: kept if is_true, unless the group around it is
// skipped.
static void open_group(int is_true)
{
  if (nconds >= MAX_IF_NEST)
    fatal0(243 /* "#if nested too deeply" */);
  if (!taking())
    cond[nconds++] = C_DEAD;
  else
    cond[nconds++] = is_true ? C_TAKING : C_WAIT;
#ifdef CCISH_STATS
  if (nconds > conds_peak)
    conds_peak = nconds;
#endif
}

// #ifdef name or #ifndef name (want 0): open a group. name, the
// directive's, is for the messages, as end_directive()'s.
#ifdef CCISH_DIAG
static void do_ifdef(int want, const char *name)
#else
#define do_ifdef(want, name) do_ifdef_(want)
static void do_ifdef_(int want)
#endif
{
  int id = macro_name(name), def = pp_is_macro(id);
  end_directive(name);
  open_group(def == want);
}

// #if expression: open a group (the expression is read only if the
// group around it is kept).
static void do_if(void)
{
  if (!taking()) {
    open_group(0);
    pp_mode = PM_SKIP;
    return;
  }
  open_group(pp_if_expr());
}

// #elif expression: the group's next branch, kept if no branch before
// it was and the expression is true (read only then).
static void do_elif(void)
{
  if (nconds == 0)
    error(240 /* "no #if before" */, "#elif");
  if (cond[nconds - 1] & C_ELSE)
    error0(260 /* "#elif after #else" */);
  if ((cond[nconds - 1] & C_STATE) == C_TAKING)
    cond[nconds - 1] = C_DONE;
  else if ((cond[nconds - 1] & C_STATE) == C_WAIT) {
    cond[nconds - 1] = pp_if_expr() ? C_TAKING : C_WAIT;
    return;
  }
  pp_mode = PM_SKIP;            // the line isn't read
}

// #error text: stop, with the text.
static void do_error(void)
{
  error_d(258 /* "#error:" */, pp_lex_rest());
}

// #line number "file": the next line has that number, and, with a
// file's name, the current file is called that from here on.
static void do_line(void)
{
  int c = pp_next(), n = pt_val;
  if ((c != TC_NUM && c != TC_UNUM) || n == 0)
    error0(261 /* "bad #line" */);
  c = pp_next();
  if (c == TC_STR) {
    if (pt_len >= MAX_PATH)
      fatal0(1 /* "output name too long" */);
    memcpy(cur_path, pt_text, pt_len);
    cur_path[pt_len] = 0;
    file_no[depth] = pp_cur_file = pp_add_file(cur_path);
    c = pp_next();
  }
  if (c != TC_NL)
    error0(261 /* "bad #line" */);
  pp_lex_set_line(n);
}

// #else: the group's last branch.
static void do_else(void)
{
  end_directive("#else");
  if (nconds == 0)
    error(240 /* "no #if before" */, "#else");
  if (cond[nconds - 1] & C_ELSE)
    error0(241 /* "#else after #else" */);
  if ((cond[nconds - 1] & C_STATE) == C_WAIT)
    cond[nconds - 1] = C_TAKING | C_ELSE;
  else if ((cond[nconds - 1] & C_STATE) == C_TAKING)
    cond[nconds - 1] = C_DONE | C_ELSE;
  else
    cond[nconds - 1] |= C_ELSE;
}

// #endif: close the group.
static void do_endif(void)
{
  end_directive("#endif");
  if (nconds == 0)
    error(240 /* "no #if before" */, "#endif");
  nconds--;
}

// Carry out a directive, after its '#'. In a group being skipped, only
// the conditional ones count, and the rest of the line is passed over;
// the null directive (a '#' alone) does nothing.
static void directive(void)
{
  char hashed[13], *name = hashed + 1;
  int n;
  pp_mode = PM_DIRECTIVE;
  hashed[0] = '#';
  n = pp_lex_word(name, sizeof(hashed) - 1);
  if (n == 0) {
    if (taking() && pp_lex() != TC_NL)
      error0(234 /* "unknown preprocessor directive:" */);
  } else if (!strcmp(name, "ifdef")) {
    do_ifdef(1, "#ifdef");
  } else if (!strcmp(name, "ifndef")) {
    do_ifdef(0, "#ifndef");
  } else if (!strcmp(name, "if")) {
    do_if();
    if (pp_mode == PM_SKIP)
      return;
  } else if (!strcmp(name, "elif")) {
    do_elif();
    if (pp_mode == PM_SKIP)
      return;
  } else if (!strcmp(name, "else")) {
    do_else();
  } else if (!strcmp(name, "endif")) {
    do_endif();
  } else if (!taking()) {
    pp_mode = PM_SKIP;          // nothing else counts here
    return;
  } else if (!strcmp(name, "define")) {
    do_define();
  } else if (!strcmp(name, "undef")) {
    do_undef();
  } else if (!strcmp(name, "include")) {
    do_include();
    return;
  } else if (!strcmp(name, "pragma")) {
    do_pragma();
  } else if (!strcmp(name, "error")) {
    do_error();
  } else if (!strcmp(name, "line")) {
    do_line();
  } else {
    error(234 /* "unknown preprocessor directive:" */, hashed);
  }
  after_directive();
}

// Add the default include directory after the -I ones: CCISH_INC if
// the build defines it (the Makefile does, for the host), else the VM's
// <syspath>ccinc (doc/bootstrap-test.md, R2). Then apply the -D and -U
// options, in order, each as a directive's line: -D name is name 1,
// and -D name=text is name text.
static void apply_options(void)
{
  static char buf[MAX_LINE + 1];    // too big for a frame
  const char *eq;
  int i;
#ifdef CCISH_INC
  pp_include_dir(CCISH_INC);
#else
  static char sys_inc[MAX_PATH];  // <syspath>ccinc
  i = cmdsys.syspath[0];
  if (i < MAX_PATH - 6) {
    memcpy(sys_inc, cmdsys.syspath + 1, i);
    strcpy(sys_inc + i, "ccinc");
    pp_include_dir(sys_inc);
  }
#endif
  pp_lex_text("__CCISH__ 1");
  do_define();
  for (i = 0; i < nopts; i++) {
    if (strlen(opt_text[i]) + 2 > MAX_LINE)
      error(233 /* "line too long (over 255 characters)" */, opt_text[i]);
    strcpy(buf, opt_text[i]);
    eq = strchr(opt_text[i], '=');
    if (opt_kind[i] == 'U') {
      pp_lex_text(buf);
      do_undef();
    } else {
      if (eq)
        buf[eq - opt_text[i]] = ' ';
      else
        strcat(buf, " 1");
      pp_lex_text(buf);
      do_define();
    }
  }
}

// The end of a file: back to the one that included it, or 0 at the
// source's own end. Its groups must all be closed.
static int end_file(void)
{
  if (nconds != file_conds[depth])
    error0(242 /* "missing #endif at the end of the file" */);
  bclose(&files[depth]);
  if (depth == 0)
    return 0;
  depth--;
  pp_lex_pop();
  pp_cur_file = file_no[depth];
  pp_file_path(pp_cur_file, cur_path);
  src_name = cur_path;
  after_directive();
  return 1;
}

#ifdef CCISH_STATS
// Report how full the tables got, on one line of stderr, for
// tools/measure.py: the name pool's bytes, then pp_mac_peak()'s, then
// the include depth and the groups open.
static void report_stats(void)
{
  int i;
  fputs("ccpp-stats", stderr);
  fputs(" ", stderr);
  fputs(num_text(pp_pool_used()), stderr);
  for (i = 0; i < 6; i++) {
    fputs(" ", stderr);
    fputs(num_text(pp_mac_peak(i)), stderr);
  }
  fputs(" ", stderr);
  fputs(num_text(depth_peak), stderr);
  fputs(" ", stderr);
  fputs(num_text(conds_peak), stderr);
  fputs("\n", stderr);
}
#endif

// Preprocess source file src into the token stream and name file of
// output name base.
void pp_file(const char *src, const char *base)
{
  int c;
  src_name = src;
  depth = 0;
  nconds = 0;
  pp_names_init();
  pp_mac_init();
  src_line = 0;
  apply_options();
  if (strlen(src) >= MAX_PATH)
    fatal0(1 /* "output name too long" */);
  strcpy(cur_path, src);
  if (!bopen(&files[0], src, 0))
    fatal_d(171 /* "cannot open" */, src);
  file_no[0] = pp_add_file(src);
  file_conds[0] = 0;
  pp_cur_file = file_no[0];
  src_name = cur_path;
  pp_out_open(base);
  pp_mode = PM_TEXT;
  pp_lex_open(&files[0]);
  for (;;) {
    c = pp_next();
    if (c == '#' && pp_from_lexer && pt_first) {
      directive();
      continue;
    }
    if (c == TC_END) {
      if (end_file())
        continue;
      break;
    }
    if (c == '#' || c == TC_PASTE)
      error0(17 /* "unexpected character in source" */);
    pp_out(c, pt_val, pt_text, pt_len);
  }
  pp_out_finish();
  src_line = 0;
#ifdef CCISH_STATS
  report_stats();
#endif
}

// After an error: close the open files, so that ProDOS can remove the
// outputs.
void pp_abort(void)
{
  while (depth >= 0)
    bclose(&files[depth--]);
  pp_out_abort();
}

// The output name for source file src: its name without ".c" (or ".C",
// as an Apple II's uppercase command line gives it), in a static
// buffer.
const char *base_of(const char *src)
{
  static char buf[MAX_PATH];
  size_t n = strlen(src);
  if (n < 3 || src[n - 2] != '.' || (src[n - 1] != 'c' && src[n - 1] != 'C'))
    fatal0(2 /* "input name must end in .c, or use -o" */);
  if (n - 2 >= sizeof(buf))
    fatal0(1 /* "output name too long" */);
  memcpy(buf, src, n - 2);
  buf[n - 2] = 0;
  return buf;
}

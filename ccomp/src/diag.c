/*
 * Diagnostics and other support shared by ccish's programs: ccish
 * (both passes), cccomp (the compile pass) and ccrel (the final pass;
 * approach.md, "B3"). Each program supplies clean_up(), which closes
 * and removes its partial output after an error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

const char *src_name = "";
int src_line;
int opt_list;
int opt_ir;
int opt_keep;

/* Return base followed by suffix, in a static buffer that the next
 * call reuses. */
const char *stream_name(const char *base, const char *suffix)
{
  static char buf[300];
  if (strlen(base) + strlen(suffix) >= sizeof(buf))
    fatal(1 /* "output name too long" */, NULL);
  strcpy(buf, base);
  strcat(buf, suffix);
  return buf;
}

/* Remove the stream files of output name base, unless -k asked to keep
 * them. */
void remove_streams(const char *base)
{
  if (!base || opt_keep)
    return;
  remove(stream_name(base, SUFFIX_DATA));
  remove(stream_name(base, SUFFIX_DREL));
  remove(stream_name(base, SUFFIX_STRS));
  remove(stream_name(base, SUFFIX_CODE));
  remove(stream_name(base, SUFFIX_SYMS));
}

/* The output name for source file src: its name without ".c", in a
 * static buffer. */
const char *base_of(const char *src)
{
  static char buf[256];
  size_t n = strlen(src);
  if (n < 3 || strcmp(src + n - 2, ".c") || n - 2 >= sizeof(buf))
    fatal(2 /* "input name must end in .c, or use -o" */, NULL);
  memcpy(buf, src, n - 2);
  buf[n - 2] = 0;
  return buf;
}

/* The message file, ccish.msg: each line is a message number, a space
 * and its text, in ascending order of number (approach.md, "C2
 * design"). It is the only copy of the text, so the programs keep none
 * in memory; a message is looked up only when it is printed. */
static char msg_path[300] = "ccish.msg";

/* Find the message file next to the program, whose name (argv[0]) is
 * argv0: in its directory, or, if it has none, the current one. */
void msg_init(const char *argv0)
{
  const char *slash = strrchr(argv0, '/');
  size_t n = slash ? (size_t)(slash - argv0) + 1 : 0;
  if (n + sizeof("ccish.msg") > sizeof(msg_path))
    return;
  memcpy(msg_path, argv0, n);
  strcpy(msg_path + n, "ccish.msg");
}

/* Copy message n's text from the message file into buf (size bytes,
 * truncating a longer one), and return 1; or return 0 if the file or
 * the message can't be found. Lines may end in CR, LF or CRLF. */
static int msg_text(int n, char *buf, int size)
{
  FILE *f = fopen(msg_path, "r");
  int c, num, len, found = 0;
  if (!f)
    return 0;
  for (;;) {
    num = 0;
    c = getc(f);
    while (c >= '0' && c <= '9') {
      num = num * 10 + c - '0';
      c = getc(f);
    }
    if (c == EOF || num > n)
      break;
    if (num == n && c == ' ') {
      for (len = 0; (c = getc(f)) != EOF && c != '\n' && c != '\r'; )
        if (len < size - 1)
          buf[len++] = (char)c;
      buf[len] = 0;
      found = 1;
      break;
    }
    while (c != EOF && c != '\n' && c != '\r')
      c = getc(f);                /* the rest of the line */
  }
  fclose(f);
  return found;
}

/* Print a diagnostic to stderr: where (the source file and line, if
 * there is one), its kind and message number, then the message's text,
 * the text of part (another message number, if not 0), and detail (if
 * not NULL): "x.c:2: error 41: lvalue required as left operand of
 * assignment". Without the message file, just the numbers:
 * "x.c:2: error 41 (194):". */
static void report(const char *kind, int msg, int part, const char *detail)
{
  char text[128];
  fflush(stdout);
  if (src_line > 0)
    fprintf(stderr, "%s:%d: ", src_name, src_line);
  else
    fprintf(stderr, "ccish: ");
  fprintf(stderr, "%s %d", kind, msg);
  if (msg_text(msg, text, sizeof(text))) {
    fprintf(stderr, ": %s", text);
    if (part && msg_text(part, text, sizeof(text)))
      fprintf(stderr, " %s", text);
    else if (part)
      fprintf(stderr, " (%d)", part);
  } else {
    if (part)
      fprintf(stderr, " (%d)", part);
    fprintf(stderr, ":");
  }
  if (detail)
    fprintf(stderr, " %s", detail);
  fprintf(stderr, "\n");
}

/* Report error msg, followed by part (another message number: how or
 * where, such as "left operand of assignment") and detail, in the
 * program being compiled; then remove partial output, and exit.
 * Compilation stops at the first error. The files are closed first, so
 * that printing (which opens the message file) needs no more memory or
 * files. */
void error_in(int msg, int part, const char *detail)
{
  clean_up();
  report("error", msg, part, detail);
  exit(1);
}

/* Report error msg, with detail. */
void error(int msg, const char *detail)
{
  error_in(msg, 0, detail);
}

/* Report warning msg, followed by part and detail: a likely mistake in
 * the program being compiled that C still allows, as gcc would warn
 * about it (such as discarding const). Compiling goes on. */
void warning_in(int msg, int part, const char *detail)
{
  report("warning", msg, part, detail);
}

/* Report warning msg, with detail. */
void warning(int msg, const char *detail)
{
  warning_in(msg, 0, detail);
}

/* Report problem msg, with detail: one that isn't the program's fault
 * (a compiler limit or a file error). Remove partial output, and exit. */
void fatal(int msg, const char *detail)
{
  clean_up();
  report("fatal", msg, 0, detail);
  exit(1);
}

/*
 * Diagnostics and other support shared by ccish's programs: ccish
 * (every pass), cccomp (the front end), ccgen (the back end; approach.md
 * 18.7) and ccrel (the final pass; approach.md, "B3"). Each program
 * supplies clean_up(), which closes and removes its partial output
 * after an error. What this file exports is declared in two headers,
 * diag.h and diag.src.h (the source position; diag.h says why).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "obj.h"

const char *src_name = "";
void (*src_name_hook)(void);
int src_line;
#ifdef CCISH_LISTING
int opt_list;
#endif
int opt_ir;
#ifdef CCISH_LISTING
int opt_dump;
#endif
int opt_keep;

/* Return base followed by suffix, in a static buffer that the next
 * call reuses. */
const char *stream_name(const char *base, const char *suffix)
{
  static char buf[MAX_PATH];
  if (strlen(base) + strlen(suffix) >= sizeof(buf))
    fatal(1 /* "output name too long" */, NULL);
  strcpy(buf, base);
  strcat(buf, suffix);
  return buf;
}

/* Remove the file of output name base and suffix (such as the IR file),
 * unless -k asked to keep it. A name too long for stream_name() can't
 * have been created, and is skipped, so that clean_up() never stops on
 * it while reporting another error. */
void remove_stream(const char *base, const char *suffix)
{
  if (base && !opt_keep && strlen(base) + strlen(suffix) < MAX_PATH)
    remove(stream_name(base, suffix));
}

/* The message file, ccish.msg: each line is a message number, a space
 * and its text, in ascending order of number (approach.md, "C2
 * design"). It is the only copy of the text, so the programs keep none
 * in memory; a message is looked up only when it is printed. */
static char msg_path[MAX_PATH] = "ccish.msg";

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
int msg_text(int n, char *buf, int size)
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

/* n in decimal, in a static buffer that the next call reuses (so copy
 * it before passing it to a diagnostic, which uses it too). Written
 * out by hand, so that ccish needs nothing of the printf family. A
 * negative n's remainders are 0 to -9 (C's % truncates), so it is
 * never negated, which would overflow for the most negative int. */
const char *num_text(int n)
{
  static char buf[12];
  int i = sizeof(buf) - 1, neg = n < 0, d;
  buf[i] = 0;
  do {
    d = n % 10;
    buf[--i] = (char)('0' + (d < 0 ? -d : d));
    n /= 10;
  } while (n != 0);
  if (neg)
    buf[--i] = '-';
  return buf + i;
}

/* Write s to stderr. */
static void put_err(const char *s)
{
  fputs(s, stderr);
}

/* Write message number part to stderr, as " (part)". */
static void put_part(int part)
{
  put_err(" (");
  put_err(num_text(part));
  put_err(")");
}

/* Print a diagnostic to stderr: where (the source file and line, if
 * there is one), its kind and message number, then the message's text,
 * the text of part (another message number, if not 0), and detail (if
 * not NULL): "x.c:2: error 41: lvalue required as left operand of
 * assignment". Without the message file, just the numbers:
 * "x.c:2: error 41 (194):". */
static void report(const char *kind, int msg, int part, const char *detail)
{
  char text[MAX_MSG];
  void (*hook)(void) = src_name_hook;
  fflush(stdout);
  if (src_line > 0) {
    if (hook) {
      src_name_hook = 0;        /* a stop inside it reports without it */
      hook();
      src_name_hook = hook;
    }
    put_err(src_name);
    put_err(":");
    put_err(num_text(src_line));
    put_err(": ");
  } else {
    put_err("ccish: ");
  }
  put_err(kind);
  put_err(" ");
  put_err(num_text(msg));
  if (msg_text(msg, text, sizeof(text))) {
    put_err(": ");
    put_err(text);
    if (part && msg_text(part, text, sizeof(text))) {
      put_err(" ");
      put_err(text);
    } else if (part) {
      put_part(part);
    }
  } else {
    if (part)
      put_part(part);
    put_err(":");
  }
  if (detail) {
    put_err(" ");
    put_err(detail);
  }
  put_err("\n");
}

/* Report a diagnostic of this kind, then remove partial output and
 * exit; an error inside clean_up() itself just reports and exits. The
 * files are closed first, so that printing (which opens the
 * message file) needs no more memory or files. detail is copied first:
 * it may be in a static buffer that clean_up() reuses (stream_name()'s,
 * when removing the stream files). The copy holds MAX_PATH, the longest
 * path ccish makes, and a ProDOS path with a prefix: only a longer host
 * path, given on the command line, is cut short. It's on the frame, not
 * static, since stop() runs once, usually at the deepest point of the
 * frame stack: a static copy would take the same memory always, and
 * the module file too (approach.md issue 24). */
static void stop(const char *kind, int msg, int part, const char *detail)
{
  static char stopping;     /* a second stop, from inside clean_up() */
  char copy[MAX_PATH];
  if (detail) {
    strncpy(copy, detail, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    detail = copy;
  }
  if (!stopping) {
    stopping = 1;
    clean_up();
  }
  report(kind, msg, part, detail);
  exit(1);
}

/* Report error msg, followed by part (another message number: how or
 * where, such as "left operand of assignment") and detail, in the
 * program being compiled; then remove partial output, and exit.
 * Compilation stops at the first error. */
void error_in(int msg, int part, const char *detail)
{
  stop("error", msg, part, detail);
}

/* Report error msg, with detail. */
void error(int msg, const char *detail)
{
  error_in(msg, 0, detail);
}

#ifdef CCISH_DIAG
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
#endif

/* Report problem msg, with detail: one that isn't the program's fault
 * (a compiler limit or a file error). Remove partial output, and exit. */
void fatal(int msg, const char *detail)
{
  stop("fatal", msg, 0, detail);
}

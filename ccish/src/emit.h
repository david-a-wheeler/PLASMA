// ccgen's stage 4 (emit.c): encode bytes, one function at a time, and
// write NAME.obj, then NAME.syms. Include after mop.h and obj.h.
#ifndef EMIT_H
#define EMIT_H

extern int fn_code[MAX_FUNCS];  // each function's start in the code
                                // segment, once written; else -1
extern int helper_size[NHELPERS];   // once written: its bytes
extern int helper_sum[NHELPERS];    // and their checksum

void emit_open(const char *base);
void emit_put(Mop *m);
void emit_byte(int c);              // one byte of a record, as is
void emit_word(int v);
int  emit_code_size(void);          // code bytes written so far
void emit_summary(void);            // close .obj, start .syms
void emit_finish(void);             // close .syms
void emit_abort(void);              // after an error
#ifdef CCISH_STATS
void emit_stats(void);              // for tools/measure.py
#endif

#endif

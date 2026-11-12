// cccomp's output (irout.c): the writer of NAME.ir.
#ifndef IROUT_H
#define IROUT_H

void ir_open(const char *base);
void ir_put(int op, int a, int f);   // f: a function, or -1
int ir_here(void);                  // IR ops written so far
int  data_string(const char *s, int len);   // pool offset of first char
int  data_offset(void);                     // data bytes written so far
void data_byte(int v);
void data_addr(int kind, int target, int add);  // a relocated word
int  data_zero(int n);                      // returns offset of n 0 bytes
int  data_drop_zeros(int mark);             // 1: all zeros from mark, gone
void ir_finish(void);                       // the summary, then close
void ir_abort(void);                        // after an error

#endif

// ccgen's input (irin.c): read NAME.ir and pass its ops to stage 2,
// writing NAME.obj and NAME.syms.
#ifndef IRIN_H
#define IRIN_H

void gen_file(const char *base);
void gen_abort(void);                       // after an error
const char *func_name(int f);       // a function's name, for messages
void remove_streams(const char *base);  // .obj and .syms, unless -k

#endif

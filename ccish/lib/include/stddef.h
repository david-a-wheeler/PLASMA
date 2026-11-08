/* stddef.h for ccish: int and pointers are 16 bits. */
#ifndef STDDEF_H
#define STDDEF_H
typedef unsigned size_t;
typedef int ptrdiff_t;
#define NULL ((void *)0)
#endif

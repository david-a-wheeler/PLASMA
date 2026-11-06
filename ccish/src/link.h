/*
 * ccrel's final pass and linker (link.c).
 */
#ifndef LINK_H
#define LINK_H

void link_module(int n, const char **bases, const char *path);
void link_abort(void);                      /* after an error */

#endif

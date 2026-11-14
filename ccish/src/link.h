// cclink's final pass and linker (link.c).
#ifndef LINK_H
#define LINK_H

#define MAX_OBJS 32         // limit link: objects in one link (ccish's own is
                            // MAX_LINK, main.c)

extern int opt_xload;       // -x: write an XLOAD image, not a REL module
                            // (../doc/xload.md, section 3)

void link_module(int n, const char **bases, const char *path,
                 const char *dir);          // -L: the library's directory,
                                            // or NULL
void link_abort(void);                      // after an error

#endif

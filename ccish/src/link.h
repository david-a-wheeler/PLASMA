// ccrel's final pass and linker (link.c).
#ifndef LINK_H
#define LINK_H

#define MAX_OBJS 32         // limit link: objects in one link (ccish's own is
                            // MAX_LINK, main.c)

void link_module(int n, const char **bases, const char *path);
void link_abort(void);                      // after an error

#endif

#include "stralloc.h"

extern void stralloc_set_die(void (*)());

extern void append(stralloc *,char *);
extern void append0(stralloc *);
extern void cat(stralloc *,stralloc *);
extern void catb(stralloc *,char *,int);
extern void cats(stralloc *,char *);
extern void copy(stralloc *,stralloc *);
extern void copyb(stralloc *,char *,int);
extern void copys(stralloc *,char *);
extern void prepends(stralloc *,char *);
extern int  starts(stralloc *,char *);
extern int  ends_with_newline(stralloc *);
extern void blank(stralloc *);

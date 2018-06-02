#include "stralloc.h"

extern void want_to_read(int,int);
extern int can_read(int);
extern int block_efficiently_until_can_read(int,int);
extern int safeappend(stralloc *,int,char *,int);
extern void safewrite(int,stralloc *);

#include "stralloc.h"

extern int can_read(int);
extern int block_efficiently_until_can_read_either(int,int);
extern int safeappend(stralloc *,int,char *,int);
extern void safewrite(int,stralloc *);

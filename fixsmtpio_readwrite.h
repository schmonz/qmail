#include "stralloc.h"

int can_read(int);
int block_efficiently_until_can_read_either(int,int);
int safeappend(stralloc *,int,char *,int);
void safewrite(int,stralloc *);

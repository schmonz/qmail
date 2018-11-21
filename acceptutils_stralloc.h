#include "stralloc.h"

void stralloc_set_die(void (*)());

void append(stralloc *,char *);
void append0(stralloc *);
void cat(stralloc *,stralloc *);
void catb(stralloc *,char *,int);
void cats(stralloc *,char *);
void copy(stralloc *,stralloc *);
void copyb(stralloc *,char *,int);
void copys(stralloc *,char *);
void prepends(stralloc *,char *);
int  starts(stralloc *,char *);
int  ends_with_newline(stralloc *);
void blank(stralloc *);

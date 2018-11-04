#include "stralloc.h"
#include "substdio.h"

extern void die_usage();
extern void die_control();
extern void die_pipe();
extern void die_fork();
extern void die_exec();
extern void die_wait();
extern void die_crash();
extern void die_read();
extern void die_write();
extern void die_nomem();
extern void die_tls();
extern void die_parse();

extern void cat(stralloc *,stralloc *);
extern void catb(stralloc *,char *,int);
extern void cats(stralloc *,char *);
extern void copy(stralloc *,stralloc *);
extern void copyb(stralloc *,char *,int);
extern void copys(stralloc *,const char *);
extern void prepends(stralloc *,const char *);
extern int  starts(stralloc *,char *);
extern int  ends_with_newline(stralloc *);
extern void blank(stralloc *);

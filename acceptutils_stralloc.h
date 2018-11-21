#include "stralloc.h"

void stralloc_set_die(void (*)(const char *,const char *));

void _append(const char *,stralloc *,char *);
#define append(a,b) _append(__func__,a,b)
void _append0(const char *,stralloc *);
#define append0(a) _append0(__func__,a)
void _cat(const char *,stralloc *,stralloc *);
#define cat(a,b) _cat(__func__,a,b)
void _catb(const char *,stralloc *,char *,int);
#define catb(a,b,c) _catb(__func__,a,b,c)
void _cats(const char *,stralloc *,char *);
#define cats(a,b) _cats(__func__,a,b)
void _copy(const char *,stralloc *,stralloc *);
#define copy(a,b) _copy(__func__,a,b)
void _copyb(const char *,stralloc *,char *,int);
#define copyb(a,b,c) _copyb(__func__,a,b,c)
void _copys(const char *,stralloc *,char *);
#define copys(a,b) _copys(__func__,a,b)

void prepends(stralloc *,char *);
int  starts(stralloc *,char *);
int  ends_with_newline(stralloc *);
void blank(stralloc *);

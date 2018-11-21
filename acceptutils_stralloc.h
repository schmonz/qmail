#include "stralloc.h"

void stralloc_set_die(void (*)(const char *,const char *));

void contextlogging_append(const char *,stralloc *,char *);
#define append(a,b) contextlogging_append(__func__,a,b)
void contextlogging_append0(const char *,stralloc *);
#define append0(a) contextlogging_append0(__func__,a)
void contextlogging_cat(const char *,stralloc *,stralloc *);
#define cat(a,b) contextlogging_cat(__func__,a,b)
void contextlogging_catb(const char *,stralloc *,char *,int);
#define catb(a,b,c) contextlogging_catb(__func__,a,b,c)
void contextlogging_cats(const char *,stralloc *,char *);
#define cats(a,b) contextlogging_cats(__func__,a,b)
void contextlogging_copy(const char *,stralloc *,stralloc *);
#define copy(a,b) contextlogging_copy(__func__,a,b)
void contextlogging_copyb(const char *,stralloc *,char *,int);
#define copyb(a,b,c) contextlogging_copyb(__func__,a,b,c)
void contextlogging_copys(const char *,stralloc *,char *);
#define copys(a,b) contextlogging_copys(__func__,a,b)

void prepends(stralloc *,char *);
int  starts(stralloc *,char *);
int  ends_with_newline(stralloc *);
void blank(stralloc *);

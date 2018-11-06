#include "acceptutils_stralloc.h"

static void (*die_sa)();

void stralloc_set_die(void (*die_nomem)()) { die_sa = die_nomem; }

void append(stralloc *to,char *from) { if (!stralloc_append(to,from)) die_sa(); }
void append0(stralloc *to) { if (!stralloc_0(to)) die_sa(); }
void cat(stralloc *to,stralloc *from) { if (!stralloc_cat(to,from)) die_sa(); }
void catb(stralloc *to,char *buf,int len) { if (!stralloc_catb(to,buf,len)) die_sa(); }
void cats(stralloc *to,char *from) { if (!stralloc_cats(to,from)) die_sa(); }
void copy(stralloc *to,stralloc *from) { if (!stralloc_copy(to,from)) die_sa(); }
void copyb(stralloc *to,char *buf,int len) { if (!stralloc_copyb(to,buf,len)) die_sa(); }
void copys(stralloc *to,char *from) { if (!stralloc_copys(to,from)) die_sa(); }
void prepends(stralloc *to,char *from) {
  stralloc tmp = {0};
  copy(&tmp,to);
  copys(to,(char *)from);
  cat(to,&tmp);
}
int starts(stralloc *haystack,char *needle) { return stralloc_starts(haystack,needle); }
int ends_with_newline(stralloc *sa) {
  return sa->len > 0 && sa->s[sa->len - 1] == '\n';
}
void blank(stralloc *sa) { copys(sa,""); }

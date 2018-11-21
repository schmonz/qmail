#include "acceptutils_stralloc.h"

static void (*die_sa)(const char *,const char *);

void stralloc_set_die(void (*die_nomem)(const char *,const char *)) {
  die_sa = die_nomem;
}

void _append(const char *caller,stralloc *to,char *from) {
  if (!stralloc_append(to,from)) die_sa(caller,__func__);
}
void _append0(const char *caller,stralloc *to) {
  if (!stralloc_0(to)) die_sa(caller,__func__);
}
void _cat(const char *caller,stralloc *to,stralloc *from) {
  if (!stralloc_cat(to,from)) die_sa(caller,__func__);
}
void _catb(const char *caller,stralloc *to,char *buf,int len) {
  if (!stralloc_catb(to,buf,len)) die_sa(caller,__func__);
}
void _cats(const char *caller,stralloc *to,char *from) {
  if (!stralloc_cats(to,from)) die_sa(caller,__func__);
}
void _copy(const char *caller,stralloc *to,stralloc *from) {
  if (!stralloc_copy(to,from)) die_sa(caller,__func__);
}
void _copyb(const char *caller,stralloc *to,char *buf,int len) {
  if (!stralloc_copyb(to,buf,len)) die_sa(caller,__func__);
}
void _copys(const char *caller,stralloc *to,char *from) {
  if (!stralloc_copys(to,from)) die_sa(caller,__func__);
}

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

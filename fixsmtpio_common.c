#include "fixsmtpio.h"
#include "fixsmtpio_common.h"
#include "readwrite.h"

void die() { _exit(1); }

static char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

static void dieerrflush(char *s) {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
  die();
}

void die_usage() { dieerrflush("usage: " PROGNAME " prog [ arg ... ]"); }
void die_control(){dieerrflush("unable to read controls"); }
void die_pipe()  { dieerrflush("unable to open pipe"); }
void die_fork()  { dieerrflush("unable to fork"); }
void die_exec()  { dieerrflush("unable to exec"); }
void die_wait()  { dieerrflush("unable to wait for child"); }
void die_crash() { dieerrflush("aack, child crashed"); }
void die_read()  { dieerrflush("unable to read"); }
void die_write() { dieerrflush("unable to write"); }
void die_nomem() { dieerrflush("out of memory"); }

void cat(stralloc *to,stralloc *from) { if (!stralloc_cat(to,from)) die_nomem(); }
void catb(stralloc *to,char *buf,int len) { if (!stralloc_catb(to,buf,len)) die_nomem(); }
void cats(stralloc *to,char *from) { if (!stralloc_cats(to,from)) die_nomem(); }
void copy(stralloc *to,stralloc *from) { if (!stralloc_copy(to,from)) die_nomem(); }
void copyb(stralloc *to,char *buf,int len) { if (!stralloc_copyb(to,buf,len)) die_nomem(); }
void copys(stralloc *to,const char *from) { if (!stralloc_copys(to,from)) die_nomem(); }
void prepends(stralloc *to,const char *from) {
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

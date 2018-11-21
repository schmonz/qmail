#include "fixsmtpio.h"
#include "fixsmtpio_die.h"
#include "readwrite.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_unistd.h"

static void die() { unistd_exit(1); }

static char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

static void errflush3(const char *caller,const char *alloc_fn,char *s) {
  substdio_puts(&sserr,PROGNAME ":");
  if (caller) {
    substdio_puts(&sserr,caller);
    substdio_puts(&sserr,":");
  }
  if (alloc_fn) {
    substdio_puts(&sserr,alloc_fn);
    substdio_puts(&sserr,":");
  }
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
}

static void errflush(char *s) {
  errflush3(0,0,s);
}

static void dieerrflush(char *s) {
  errflush(s);
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
void die_nomem(const char *caller,const char *alloc_fn) {
  errflush3(caller,alloc_fn,"out of memory");
  die();
}
void die_tls()   { dieerrflush("TLS temporarily not available"); }
void die_parse() {    errflush("unable to parse control/fixsmtpio");
                      unistd_exit(EXIT_NOW_PARSEFAIL); }

void logit(char logprefix,stralloc *sa) {
  if (!env_get("FIXSMTPIODEBUG")) return;
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_put(&sserr,&logprefix,1);
  substdio_puts(&sserr,": ");
  substdio_put(&sserr,sa->s,sa->len);
  if (!ends_with_newline(sa)) substdio_puts(&sserr,"\r\n");
  substdio_flush(&sserr);
}

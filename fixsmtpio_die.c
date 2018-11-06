#include "fixsmtpio.h"
#include "fixsmtpio_die.h"
#include "readwrite.h"

static void die() { _exit(1); }

static char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

static void errflush(char *s) {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
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
void die_nomem() { dieerrflush("out of memory"); }
void die_tls()   { dieerrflush("TLS temporarily not available"); }
void die_parse() {    errflush("unable to parse control/fixsmtpio");
                      _exit(EXIT_NOW_PARSEFAIL); }

#include "exit.h"
#include "readwrite.h"
#include "substdio.h"

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,"dieifuid0: ");
  substdio_puts(&sserr,s);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

void die() { _exit(1); }
void die_usage() { errflush("usage: dieifuid0 prog"); die(); }
void die_root() { errflush("WAS RUNNING AS ROOT, TERMINATING"); die(); }

int main(int argc,char **argv) {
  char **childargs;
 
  childargs = argv + 1;
  if (!*childargs) die_usage();

  if (getuid() == 0) die_root();
 
  execvp(*childargs,childargs);
  die();
}

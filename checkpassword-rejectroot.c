#include "exit.h"
#include "readwrite.h"
#include "substdio.h"

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,s);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

void die() { _exit(1); }
void die_usage() { errflush("usage: checkpassword-rejectroot prog"); die(); }
void die_root() { errflush("checkpassword-rejectroot: am root"); die(); }

int main(int argc,char **argv) {
  char **childargs;
 
  childargs = argv + 1;
  if (!*childargs) die_usage();

  if (getuid() == 0) {
    errflush("checkpassword-rejectroot: ROOT LOGIN ATTEMPTED");
    die_root();
  }
 
  execvp(*childargs,childargs);
  die();
}

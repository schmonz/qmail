#include "readwrite.h"
#include "substdio.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,s);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

void die_usage() { errflush("usage: qmail-fixsmtpio prog"); die(); }

int main(int argc,char **argv) {
  argv += 1;

  if (!*argv) die_usage();
    execvp(*argv,argv);
}

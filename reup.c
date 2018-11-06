#include "env.h"
#include "fmt.h"
#include "readwrite.h"
#include "scan.h"
#include "sgetopt.h"
#include "str.h"
#include "substdio.h"
#include "wait.h"

#include "acceptutils_unistd.h"

#define PROGNAME "reup"

void die() { unistd_exit(1); }

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
}

void dieerrflush(char *s) {
  errflush(s);
  die();
}

void die_usage() { dieerrflush("usage: " PROGNAME " [ -t tries ] prog"); }
void die_fork()  { dieerrflush("unable to fork"); }
void die_nomem() { dieerrflush("out of memory"); }

void logtry(char *try,char *progname) {
  substdio_puts(&sserr,PROGNAME ": try ");
  substdio_puts(&sserr,try);
  substdio_puts(&sserr,": ");
  substdio_puts(&sserr,progname);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

int try(int attempt,char **childargs) {
  int child;
  int wstat;
  char reup[FMT_ULONG];

  switch ((child = unistd_fork())) {
    case -1:
      die_fork();
    case 0:
      str_copy(reup + fmt_ulong(reup,attempt),"");
      if (!env_put2("REUP",reup)) die_nomem();
      logtry(reup,*childargs);
      unistd_execvp(*childargs,childargs);
      die(); // log something I guess
  }

  if (wait_pid(&wstat,child) == -1) die(); //die why? log something
  if (wait_crashed(wstat)) die(); //die why? log something

  return wait_exitcode(wstat);
}

int keep_trying(int attempt,int max) {
  if (max == 0) return 1;
  if (attempt <= max) return 1;
  return 0;
}

int stop_trying(int exitcode) {
  switch (exitcode) {
    case 0:
      errflush("success");
      return 1;
    case 12:
      errflush("permanent failure");
      return 1;
    default:
      return 0;
  }
}

int main(int argc,char **argv) {
  int exitcode;
  int opt;
  int tries;
  int i;

  tries = 0;
  while ((opt = getopt(argc,argv,"t:")) != opteof) {
    switch (opt) {
      case 't':
        if (!scan_ulong(optarg,&tries)) die_usage();
        break;
      default:
        die_usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (!*argv) die_usage();

  for (i = 1; keep_trying(i,tries); i++) {
    exitcode = try(i,argv);
    if (stop_trying(exitcode)) unistd_exit(exitcode);
  }

  errflush("no more tries");
  unistd_exit(exitcode);
}

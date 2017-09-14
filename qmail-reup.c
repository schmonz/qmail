#include "env.h"
#include "fmt.h"
#include "readwrite.h"
#include "scan.h"
#include "sgetopt.h"
#include "str.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,s);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

void die_usage() { errflush("usage: qmail-reup [ -t tries ] subprogram"); die(); }
void die_fork()  { errflush("qmail-reup unable to fork"); die(); }
void die_nomem() { errflush("qmail-reup out of memory"); die(); }

int try(int attempt,char **childargs) {
  int child;
  int wstat;
  char reup[FMT_ULONG];

  switch (child = fork()) {
    case -1:
      die_fork();
      break;
    case 0:
      str_copy(reup + fmt_ulong(reup,attempt),"");
      if (!env_put2("REUP",reup)) die_nomem();
      execvp(*childargs,childargs);
      die();
  }

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

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
    case 12:
      return 1;
    default:
      return 0;
  }
}

int main(int argc,char **argv) {
  int exitcode;
  int opt;
  int tries;

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

  for (int i = 1; keep_trying(i,tries); i++) {
    exitcode = try(i,argv);
    if (stop_trying(exitcode)) _exit(exitcode);
  }

  _exit(exitcode);
}

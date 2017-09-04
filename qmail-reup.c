#include "readwrite.h"
#include "sig.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void err(char *s) { substdio_puts(&sserr,s); }
void flush() { substdio_flush(&sserr); }

void die_usage() { err("usage: qmail-reup subprogram\n"); flush(); die(); }

char **childargs;

int run_args(void) {
  int child;
  int wstat;
  int exitcode;

  switch (child = fork()) {
    case -1:
      die();
      break;
    case 0:
      execvp(*childargs,childargs);
      die();
  }
  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();
  exitcode = wait_exitcode(wstat);
  if (exitcode == 0) {
    err("exiting zero\n");
  } else {
    err("exiting nonzero\n");
  }
  flush();

  return exitcode;
}

int main(int argc,char **argv) {
  int exitcode;

  sig_alarmcatch(die);
  sig_pipeignore();
 
  childargs = argv + 1;
  if (!*childargs) die_usage();

  for (int i = 0; i < 3; i++) {
    if (0 == (exitcode = run_args()))
      _exit(exitcode);
  }

  _exit(exitcode);
}

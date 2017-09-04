#include "readwrite.h"
#include "sig.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

void out(char *s) { substdio_puts(&ssout,s); }
void flush() { substdio_flush(&ssout); }

void die_usage() { out("usage: qmail-reup subprogram\n"); flush(); die(); }

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
    out("exiting zero\n");
    flush();
  } else {
    out("exiting nonzero\n");
    flush();
  }

  return exitcode;
}

int main(int argc,char **argv) {
  sig_alarmcatch(die);
  sig_pipeignore();
 
  childargs = argv + 1;
  if (!*childargs) die_usage();

  if (run_args() != 0)
    _exit(run_args());
  die();
}

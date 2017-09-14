#include "env.h"
#include "fmt.h"
#include "readwrite.h"
#include "str.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,s);
  substdio_flush(&sserr);
}

void die_usage() { errflush("usage: qmail-reup subprogram\n"); die(); }
void die_fork() { errflush("qmail-reup unable to fork\n"); die(); }
void die_nomem() { errflush("qmail-reup out of memory\n"); die(); }

int try(int attempt, char **childargs) {
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
  char **childargs;

  childargs = argv + 1;
  if (!*childargs) die_usage();

  for (int i = 1; i <= 3; i++) {
    exitcode = try(i, childargs);
    if (stop_trying(exitcode)) _exit(exitcode);
  }

  _exit(exitcode);
}

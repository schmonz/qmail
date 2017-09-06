#include "getln.h"
#include "readwrite.h"
#include "sig.h"
#include "stralloc.h"
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
void die_pipe() { errflush("qmail-reup unable to open pipe\n"); die(); }
void die_fork() { errflush("qmail-reup unable to fork\n"); die(); }
void die_read() { errflush("qmail-reup unable to read from pipe\n"); die(); }
void die_nomem() { errflush("qmail-reup out of memory\n"); die(); }

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);
char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

char **childargs;

int read_line(stralloc *into) {
  int match;

  if (getln(&ssin,into,&match,'\n') == -1) die_read();

  if (match == 0) return 0; // final partial line

  if (!stralloc_0(into)) die_nomem();

  return 1;
}

void putsflush(char *s) {
  substdio_puts(&ssout,s);
  substdio_flush(&ssout);
}

void filter_output(int runs) {
  stralloc line = {0};
  for (int lineno = 0; read_line(&line); lineno++) {
    if (lineno > 0 || runs == 0) putsflush(line.s);
  }
}

int run_args() {
  static int runs = 0;
  int exitcode;
  int child;
  int wstat;
  int pi[2];

  if (pipe(pi) == -1) die_pipe();

  switch (child = fork()) {
    case -1:
      die_fork();
      break;
    case 0:
      close(pi[0]);
      if (fd_move(1,pi[1]) == -1) die_pipe();
      execvp(*childargs,childargs);
      die();
  }
  close(pi[1]);
  if (fd_move(0,pi[0]) == -1) die_pipe();
  // XXX what if lines-to-hide >= lines-before-EOF?
  // like if there's one line and we want to skip one line
  filter_output(runs++);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  exitcode = wait_exitcode(wstat);
  if (exitcode == 0) {
    errflush("exiting zero\n");
  } else {
    errflush("exiting nonzero\n");
  }
  close(pi[0]);
  close(1);

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

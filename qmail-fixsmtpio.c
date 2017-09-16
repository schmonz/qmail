#include "fd.h"
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

void die_usage() { errflush("usage: qmail-fixsmtpio prog\n"); die(); }
void die_pipe()  { errflush("qmail-fixsmtpio: unable to open pipe\n"); die(); }
void die_fork()  { errflush("qmail-fixsmtpio: unable to fork\n"); die(); }
void die_read()  { errflush("qmail-fixsmtpio: unable to read\n"); die(); }
void die_write() { errflush("qmail-fixsmtpio: unable to write\n"); die(); }
void die_nomem() { errflush("qmail-fixsmtpio: out of memory\n"); die(); }

char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

int read_line(stralloc *into) {
  int match;
  if (getln(&ssin,into,&match,'\n') == -1) die_read();
  if (!stralloc_0(into)) die_nomem();
  if (match == 0) return 0; // XXX allow partial final line?

  return 1;
}

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

void putsflush(char *s) {
  substdio_puts(&ssout,s);
  substdio_flush(&ssout);
}

void log_output() {
  stralloc line = {0};
  while (read_line(&line)) {
    putsflush(line.s);
		errflush(line.s);
  }
}

void write_stdout_to(int fd) {
  if (fd_move(1,fd) == -1) die_pipe();
}

void read_stdin_from(int fd) {
  if (fd_move(0,fd) == -1) die_pipe();
}

int run_child(char **childargs) {
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
      write_stdout_to(pi[1]);
      execvp(*childargs,childargs);
      die();
  }
  close(pi[1]);

  read_stdin_from(pi[0]);
  log_output();

  close(pi[0]);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  return wait_exitcode(wstat);
}

int main(int argc,char **argv) {
  argv += 1;
  if (!*argv) die_usage();
  _exit(run_child(argv));
}

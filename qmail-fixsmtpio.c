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

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

void putsflush(char *s,substdio *to) {
  substdio_puts(to,s);
  substdio_flush(to);
}

void errflush(char *s) { putsflush(s,&sserr); }

void die_usage() { errflush("usage: qmail-fixsmtpio prog\n"); die(); }
void die_pipe()  { errflush("qmail-fixsmtpio: unable to open pipe\n"); die(); }
void die_fork()  { errflush("qmail-fixsmtpio: unable to fork\n"); die(); }
void die_read()  { errflush("qmail-fixsmtpio: unable to read\n"); die(); }
void die_write() { errflush("qmail-fixsmtpio: unable to write\n"); die(); }
void die_nomem() { errflush("qmail-fixsmtpio: out of memory\n"); die(); }

int read_line(stralloc *into,substdio *from) {
  int match;
  if (getln(from,into,&match,'\n') == -1) die_read();
  if (!stralloc_0(into)) die_nomem();
  if (match == 0) return 0; // XXX allow partial final line?

  return 1;
}

void log_output(substdio *from,substdio *to) {
  stralloc line = {0};
  while (read_line(&line,from)) {
    putsflush(line.s,to);
    errflush("OUT: ");
		errflush(line.s);
  }
}

void switch_stdout(int fd) {
  if (fd_move(1,fd) == -1) die_pipe();
}

void switch_stdin(int fd) {
  if (fd_move(0,fd) == -1) die_pipe();
}

int run_child(char **childargs) {
  int child;
  int wstat;
  enum { FROM = 0, TO = 1 };
  int fromkid_toproxy[2], from_kid, to_proxy;

  if (pipe(fromkid_toproxy) == -1) die_pipe();
  from_kid = fromkid_toproxy[FROM];
  to_proxy = fromkid_toproxy[TO];

  switch (child = fork()) {
    case -1:
      die_fork();
      break;
    case 0:
      close(from_kid);
      switch_stdout(to_proxy);
      execvp(*childargs,childargs);
      die();
  }
  close(to_proxy);

  switch_stdin(from_kid);
  log_output(&ssin,&ssout);

  close(from_kid);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  return wait_exitcode(wstat);
}

int main(int argc,char **argv) {
  argv += 1;
  if (!*argv) die_usage();
  _exit(run_child(argv));
}
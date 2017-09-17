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
  int fromserver_toproxy[2], from_server, to_proxy;
  int fromproxy_toserver[2], from_proxy, to_server;

  if (pipe(fromserver_toproxy) == -1) die_pipe();
  from_server = fromserver_toproxy[FROM];
  to_proxy    = fromserver_toproxy[TO];
  if (pipe(fromproxy_toserver) == -1) die_pipe();
  from_proxy  = fromproxy_toserver[FROM];
  to_server   = fromproxy_toserver[TO];

  switch (child = fork()) {
    case -1:
      die_fork();
      break;
    case 0:
      close(from_server);
      switch_stdin(from_proxy);
      close(to_server);
      switch_stdout(to_proxy);
      execvp(*childargs,childargs);
      die();
  }
  close(from_proxy);
  close(to_proxy);

  stralloc line = {0};

  //requests: read stdin, write to_server
  char sstoserverbuf[128];
  substdio sstoserver = SUBSTDIO_FDBUF(write,to_server,sstoserverbuf,sizeof sstoserverbuf);
  while (read_line(&line,&ssin)) {
    putsflush(line.s,&sstoserver);
    errflush("IN: ");
    errflush(line.s);
  }

  //responses: read from_server, write stdout
  char ssfromserverbuf[128];
  substdio ssfromserver = SUBSTDIO_FDBUF(read,from_server,ssfromserverbuf,sizeof ssfromserverbuf);
  while (read_line(&line,&ssfromserver)) {
    putsflush(line.s,&ssout);
    errflush("OUT: ");
    errflush(line.s);
  }

  close(from_server);
  close(to_server);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  return wait_exitcode(wstat);
}

int main(int argc,char **argv) {
  argv += 1;
  if (!*argv) die_usage();
  _exit(run_child(argv));
}

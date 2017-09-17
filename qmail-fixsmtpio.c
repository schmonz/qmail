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

void die_usage() { errflush("usage: qmail-fixsmtpio prog [ arg ... ]\n"); die(); }
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

void mypipe(int *from,int *to) {
  int pi[2];
  if (pipe(pi) == -1) die_pipe();
  *from = pi[0];
  *to = pi[1];
}

void be_child(int from_proxy,int to_proxy,int from_server,int to_server,char **argv) {
  close(from_server);
  switch_stdin(from_proxy);
  close(to_server);
  switch_stdout(to_proxy);
  execvp(*argv,argv);
  die();
}

void setup_proxy(int from_proxy,int to_proxy) {
  close(from_proxy);
  close(to_proxy);
}

void do_proxy_stuff(int from_client,int to_server,int from_server,int to_client) {
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
}

void teardown_proxy_and_exit(int child,int from_server,int to_server) {
  int wstat;

  close(from_server);
  close(to_server);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  _exit(wait_exitcode(wstat));
}

void be_parent(int child,int from_client,int to_client,int from_proxy,int to_proxy,int from_server,int to_server) {

  setup_proxy(from_proxy,to_proxy);
  do_proxy_stuff(from_client,to_server,from_server,to_client);

  teardown_proxy_and_exit(child,from_server,to_server);
}

int main(int argc,char **argv) {
  int child;
  int from_proxy,  to_proxy;
  int from_server, to_server;
  int from_client, to_client;

  argv += 1; if (!*argv) die_usage();

  mypipe(&from_server, &to_proxy);
  mypipe(&from_proxy, &to_server);

  from_client = 0;
  to_client = 1;

  if (child = fork())
    be_parent(child,from_client,to_client,from_proxy,to_proxy,from_server,to_server);
  else if (child == 0)
    be_child(from_proxy,to_proxy,from_server,to_server,argv);
  else
    die_fork();
}

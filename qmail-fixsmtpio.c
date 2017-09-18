#include "error.h"
#include "fd.h"
#include "getln.h"
#include "readwrite.h"
#include "select.h"
#include "sig.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

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

int is_entire_line(stralloc *sa) {
  return sa->s[sa->len - 1] == '\n';
}

void want_to_read(int fd,fd_set *fds) {
  FD_SET(fd,fds);
}

int can_read(int fd,fd_set *fds) {
  return FD_ISSET(fd,fds);
}

int max(int a,int b) {
  if (a > b) return a;
  return b;
}

int can_read_something(int fd1,int fd2,fd_set *fds) {
  int ready;
  ready = select(1+max(fd1,fd2),fds,(fd_set *)0,(fd_set *)0,(struct timeval *) 0);
  if (ready == -1 && errno != error_intr) die_read();
  return ready;
}

void logio(char *logprefix,stralloc *sa,substdio *to) {
  if (!stralloc_0(sa)) die_nomem();
  putsflush(sa->s,to);
  errflush(logprefix);
  errflush(sa->s);
  if (!stralloc_copys(sa,"")) die_nomem();
}

void do_proxy_stuff(int from_client,int to_server,int from_server,int to_client) {
  stralloc request = {0};
  stralloc response = {0};
  fd_set fds;
  char buf[128];
  int num_bytes_read;

  char sstoserverbuf[128];
  substdio sstoserver = SUBSTDIO_FDBUF(write,to_server,sstoserverbuf,sizeof sstoserverbuf);

  for (;;) {
    FD_ZERO(&fds);
    want_to_read(from_client,&fds);
    want_to_read(from_server,&fds);

    if (!can_read_something(from_client,from_server,&fds)) continue;

    if (can_read(from_client,&fds)) {
      num_bytes_read = read(from_client,buf,sizeof buf);
      if (num_bytes_read == -1 && errno != error_intr) die_read();
      if (num_bytes_read == 0) break;
      if (!stralloc_catb(&request,buf,num_bytes_read)) die_nomem();
      if (is_entire_line(&request)) {
        logio("I: ",&request,&sstoserver);
      }
    }

    if (can_read(from_server,&fds)) {
      num_bytes_read = read(from_server,buf,sizeof buf);
      if (num_bytes_read == -1 && errno != error_intr) die_read();
      if (num_bytes_read == 0) break;
      if (!stralloc_catb(&response,buf,num_bytes_read)) die_nomem();
      if (is_entire_line(&response)) {
        logio("O: ",&response,&ssout);
      }
    }
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

  if ((child = fork()))
    be_parent(child,from_client,to_client,from_proxy,to_proxy,from_server,to_server);
  else if (child == 0)
    be_child(from_proxy,to_proxy,from_server,to_server,argv);
  else
    die_fork();
}

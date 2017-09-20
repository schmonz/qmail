#include "case.h"
#include "error.h"
#include "fd.h"
#include "readwrite.h"
#include "select.h"
#include "str.h"
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

int saferead(int fd,char *buf,int len) {
  int r;
  r = read(fd,buf,len);
  if (r == -1) if (errno != error_intr) die_read();
  return r;
}

int safeappend(stralloc *sa,int fd,char *buf,int len) {
  int r = saferead(fd,buf,len);
  if (!stralloc_catb(sa,buf,r)) die_nomem();
  return r;
}

void send_data(int to_server,stralloc data) {
  if (write(to_server,data.s,data.len) == -1) die_write();
}

int is_last_line_of_data(stralloc r) {
  return (r.len == 3 && r.s[0] == '.' && r.s[1] == '\r' && r.s[2] == '\n');
}

void parse_request(stralloc request,stralloc *verb,stralloc *arg) {
  stralloc chomped = {0};
  int first_space;

  if (!stralloc_copyb(&chomped,request.s,request.len - 1)) die_nomem();
  first_space = str_chr(chomped.s,' ');

  if (first_space <= 0 || first_space >= chomped.len) {
    if (!stralloc_copy(verb,&chomped)) die_nomem();
    if (!stralloc_copys(arg,"")) die_nomem();
  } else {
    if (!stralloc_copyb(verb,chomped.s,first_space)) die_nomem();
    if (!stralloc_copyb(arg,chomped.s + first_space + 1,chomped.len - first_space - 1)) die_nomem();
  }
  case_lowerb(verb->s,verb->len);
}

void send_request(int to_server,stralloc *verb,stralloc *arg) {
  stralloc r = {0};
  if (!stralloc_copyb(&r,verb->s,verb->len)) die_nomem();
  if (arg->len) {
    if (!stralloc_cats(&r," ")) die_nomem();
    if (!stralloc_catb(&r,arg->s,arg->len)) die_nomem();
  }
  if (!stralloc_cats(&r,"\r\n")) die_nomem();
  if (write(to_server,r.s,r.len) == -1) die_write();
}

void send_response(int to_client,char *response) {
  if (write(to_client,response,str_len(response)) == -1) die_write();
}

char *smtp_test(stralloc *verb,stralloc *arg) {
  stralloc response = {0};
  if (!stralloc_copys(&response,"250 qmail-fixsmtpio test ok: ")) die_nomem();
  if (!stralloc_catb(&response,arg->s,arg->len)) die_nomem();
  if (!stralloc_cats(&response,"\r\n")) die_nomem();
  if (!stralloc_0(&response)) die_nomem();
  return response.s;
}

char *smtp_auth(stralloc *verb,stralloc *arg) {
  stralloc response = {0};
  if (!stralloc_copys(&response,"502 unimplemented (#5.5.1)")) die_nomem();
  if (!stralloc_cats(&response,"\r\n")) die_nomem();
  if (!stralloc_0(&response)) die_nomem();
  return response.s;
}

int verb_matches(char *s,stralloc *sa) {
  if (!sa->len) return 0;
  return !str_diffn(s,sa->s,sa->len);
}

void *handle_internally(stralloc *verb,stralloc *arg) {
  if (verb_matches("test",verb)) return smtp_test;
  if (verb_matches("auth",verb)) return smtp_auth;

  return 0;
}

void dispatch_request(stralloc *verb,stralloc *arg,int to_server,int to_client) {
  char *(*internalfn)();
  if ((internalfn = handle_internally(verb,arg))) {
    send_response(to_client,internalfn(verb,arg));
  } else {
    send_request(to_server,verb,arg);
  }
}

void handle_request(int from_client,int to_server,int to_client,stralloc request,stralloc *verb,stralloc *arg,int *want_data,int *in_data) {
  if (*in_data) {
    send_data(to_server,request);
    if (is_last_line_of_data(request)) {
      *in_data = 0;
    }
  } else {
    parse_request(request,verb,arg);
    if (verb_matches("data",verb)) {
      *want_data = 1;
    }
    dispatch_request(verb,arg,to_server,to_client);
  }
}

void do_proxy_stuff(int from_client,int to_server,int from_server,int to_client) {
  fd_set fds;
  char buf[128];
  stralloc request = {0}, verb = {0}, arg = {0}, response = {0};
  int want_data = 0, in_data = 0;

  for (;;) {
    FD_ZERO(&fds);
    want_to_read(from_client,&fds);
    want_to_read(from_server,&fds);

    if (!can_read_something(from_client,from_server,&fds))
      continue;

    if (can_read(from_client,&fds)) {
      if (!safeappend(&request,from_client,buf,sizeof buf))
        break;
      if (is_entire_line(&request)) {
        handle_request(from_client,to_server,to_client,request,&verb,&arg,&want_data,&in_data);
        substdio_putsflush(&sserr,"I: ");
        substdio_putflush(&sserr,request.s,request.len);
        if (!stralloc_copys(&request,"")) die_nomem();
      }
    }

    if (can_read(from_server,&fds)) {
      if (!safeappend(&response,from_server,buf,sizeof buf))
        break;
      if (is_entire_line(&response))
        substdio_putflush(&ssout,response.s,response.len);
        substdio_putsflush(&sserr,"O: ");
        substdio_putflush(&sserr,response.s,response.len);
        if (!stralloc_copys(&response,"")) die_nomem();
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

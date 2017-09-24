#include "case.h"
#include "error.h"
#include "fd.h"
#include "getln.h"
#include "readwrite.h"
#include "select.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) { substdio_putsflush(&sserr,s); }

void die_usage() { errflush("usage: qmail-fixsmtpio prog [ arg ... ]\n"); die(); }
void die_pipe()  { errflush("qmail-fixsmtpio: unable to open pipe\n"); die(); }
void die_fork()  { errflush("qmail-fixsmtpio: unable to fork\n"); die(); }
void die_read()  { errflush("qmail-fixsmtpio: unable to read\n"); die(); }
void die_write() { errflush("qmail-fixsmtpio: unable to write\n"); die(); }
void die_nomem() { errflush("qmail-fixsmtpio: out of memory\n"); die(); }

void strip_last_eol(stralloc *sa) {
  if (sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->s[sa->len-1] == '\r') sa->len--;
}

void munge_greeting(stralloc *response) {
  if (!stralloc_copys(response,"235 ok go ahead (#2.0.0)")) die_nomem();
}

void munge_test(stralloc *response) {
  stralloc_cats(response," and also it's mungeable");
}

int verb_matches(char *s,stralloc *sa) {
  if (!sa->len) return 0;
  return !case_diffb(s,sa->len,sa->s);
}

void munge_response(stralloc *response,stralloc *verb,stralloc *arg) {
  strip_last_eol(response);
  if (verb_matches("greeting",verb)) munge_greeting(response);
  if (verb_matches("test",verb)) munge_test(response);
  stralloc_cats(response,"\r\n");
}

void use_as_stdin(int fd) {
  if (fd_move(0,fd) == -1) die_pipe();
}

void use_as_stdout(int fd) {
  if (fd_move(1,fd) == -1) die_pipe();
}

void mypipe(int *from,int *to) {
  int pi[2];
  if (pipe(pi) == -1) die_pipe();
  *from = pi[0];
  *to = pi[1];
}

void setup_server(int from_proxy,int to_server,
                  int from_server,int to_proxy) {
  close(from_server);
  close(to_server);
  use_as_stdin(from_proxy);
  use_as_stdout(to_proxy);
}

void exec_server_and_never_return(char **argv) {
  execvp(*argv,argv);
  die();
}

void be_child(int from_proxy,int to_proxy,
              int from_server,int to_server,
              char **argv) {
  setup_server(from_proxy,to_server,from_server,to_proxy);
  exec_server_and_never_return(argv);
}

void setup_proxy(int from_proxy,int to_proxy) {
  close(from_proxy);
  close(to_proxy);
}

int is_entire_line(stralloc *sa) {
  return sa->s[sa->len - 1] == '\n';
}

fd_set fds;

void want_to_read(int fd) {
  FD_SET(fd,&fds);
}

int can_read(int fd) {
  return FD_ISSET(fd,&fds);
}

int max(int a,int b) {
  if (a > b) return a;
  return b;
}

int can_read_something(int fd1,int fd2) {
  int ready;
  ready = select(1+max(fd1,fd2),&fds,(fd_set *)0,(fd_set *)0,(struct timeval *) 0);
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
  int r;
  r = saferead(fd,buf,len);
  if (!stralloc_catb(sa,buf,r)) die_nomem();
  return r;
}

int is_last_line_of_data(stralloc *r) {
  return (r->len == 3 && r->s[0] == '.' && r->s[1] == '\r' && r->s[2] == '\n');
}

void parse_request(stralloc *request,stralloc *verb,stralloc *arg) {
  stralloc chomped = {0};
  int len;
  int first_space;

  len = request->len;

  if (!stralloc_0(request)) die_nomem();
  first_space = str_chr(request->s,' ');

  // XXX strip_last_eol();
  if (request->s[len-1] == '\n') len--;
  if (request->s[len-1] == '\r') len--;
  if (!stralloc_copyb(&chomped,request->s,len)) die_nomem();

  if (first_space <= 0 || first_space >= chomped.len) {
    if (!stralloc_copy(verb,&chomped)) die_nomem();
    if (!stralloc_copys(arg,"")) die_nomem();
  } else {
    if (!stralloc_copyb(verb,chomped.s,first_space)) die_nomem();
    if (!stralloc_copyb(arg,chomped.s + first_space + 1,chomped.len - first_space - 1)) die_nomem();
  }

  if (!stralloc_copy(request,&chomped)) die_nomem();
  if (!stralloc_cats(request,"\r\n")) die_nomem();
}

void logit(char logprefix,stralloc *sa) {
  substdio_putflush(&sserr,&logprefix,1);
  substdio_putsflush(&sserr,": ");
  substdio_putflush(&sserr,sa->s,sa->len);
}

void write_to_client(int fd,stralloc *sa) {
  if (write(fd,sa->s,sa->len) == -1) die_write();
  logit('O',sa);
}

void write_to_server(int fd,stralloc *sa) {
  if (write(fd,sa->s,sa->len) == -1) die_write();
  logit('I',sa);
}

char *smtp_test(stralloc *verb,stralloc *arg) {
  stralloc response = {0};
  if (!stralloc_copys(&response,"250 qmail-fixsmtpio test ok: ")) die_nomem();
  if (!stralloc_catb(&response,arg->s,arg->len)) die_nomem();
  if (!stralloc_cats(&response,"\r\n")) die_nomem();
  if (!stralloc_0(&response)) die_nomem();
  return response.s;
}

char *smtp_unimplemented(stralloc *verb,stralloc *arg) {
  stralloc response = {0};
  if (!stralloc_copys(&response,"502 unimplemented (#5.5.1)")) die_nomem();
  if (!stralloc_cats(&response,"\r\n")) die_nomem();
  if (!stralloc_0(&response)) die_nomem();
  return response.s;
}

void *handle_internally(stralloc *verb,stralloc *arg) {
  if (verb_matches("test",verb)) return smtp_test(verb,arg);
  if (verb_matches("auth",verb)) return smtp_unimplemented(verb,arg);
  if (verb_matches("starttls",verb)) return smtp_unimplemented(verb,arg);

  return 0;
}

void send_keepalive(int server,stralloc *request) {
  stralloc keepalive = {0};
  if (!stralloc_copys(&keepalive,"NOOP ")) die_nomem();
  if (!stralloc_cat(&keepalive,request)) die_nomem();
  write_to_server(server,&keepalive);
}

void check_keepalive(int client, stralloc *response) {
  if (!stralloc_starts(response,"250 ok")) {
    write_to_client(client,response);
    die();
  }
}

char *blocking_line_read(int fd) {
  char buf[128];
  substdio ss;
  stralloc line = {0};
  int match;

  substdio_fdbuf(&ss,saferead,fd,buf,sizeof buf);
  if (getln(&ss,&line,&match,'\n') == -1) die_nomem();
  if (!stralloc_0(&line)) die_nomem();
  return line.s;
}

struct request_response {
  stralloc *request;
  stralloc *verb;
  stralloc *arg;
  stralloc *response;
};

void handle_request(int from_client,int to_server,
                    int from_server,int to_client,
                    stralloc *request,stralloc *verb,stralloc *arg,
                    int *want_data,int *in_data) {
  char *internal_response;
  stralloc sa_internal_response = {0};
  stralloc sa_keepalive_response = {0};

  if (*in_data) {
    write_to_server(to_server,request);
    if (is_last_line_of_data(request)) {
      *in_data = 0;
    }
  } else {
    if ((internal_response = handle_internally(verb,arg))) {
/*
 * 1. Add our response to the queue
 * 2. Add a keepalive request to the queue
 * 3. Catch the keepalive response, and munge it
 */
      send_keepalive(to_server,request);
      if (!stralloc_copys(&sa_keepalive_response,blocking_line_read(from_server))) die_nomem();
      check_keepalive(to_client,&sa_keepalive_response);
      logit('O',&sa_keepalive_response);

      logit('I',request);
      if (!stralloc_copys(&sa_internal_response,internal_response)) die_nomem();
      munge_response(&sa_internal_response,verb,arg);
      write_to_client(to_client,&sa_internal_response);
      if (!stralloc_copys(verb,"")) die_nomem();
      if (!stralloc_copys(arg,"")) die_nomem();
    } else {
      if (verb_matches("data",verb)) *want_data = 1;
      write_to_server(to_server,request);
    }
  }
}

void handle_response(int to_client,stralloc *response,
                     stralloc *verb,stralloc *arg) {
  munge_response(response,verb,arg);
  write_to_client(to_client,response);
}

stralloc single_queued_request = {0};

void dequeue_request(stralloc *sa) {
  if (!stralloc_copy(sa,&single_queued_request)) die_nomem();
  if (!stralloc_copys(&single_queued_request,"")) die_nomem();
}

void queue_request(stralloc *sa) {
  if (!stralloc_copy(&single_queued_request,sa)) die_nomem();
  if (!stralloc_copys(sa,"")) die_nomem();
}

stralloc single_queued_response = {0};

void dequeue_response(stralloc *sa) {
  if (!stralloc_copy(sa,&single_queued_response)) die_nomem();
  if (!stralloc_copys(&single_queued_response,"")) die_nomem();
}

void queue_response(stralloc *sa) {
  if (!stralloc_copy(&single_queued_response,sa)) die_nomem();
  if (!stralloc_copys(sa,"")) die_nomem();
}

void do_proxy_stuff(int from_client,int to_server,
                    int from_server,int to_client) {
  char buf[128];
  int want_data = 0, in_data = 0;
  struct request_response rr;

  stralloc partial_request = {0};
  stralloc partial_response = {0};
  // XXX do not touch these directly
  stralloc request = {0}, verb = {0}, arg = {0}, response = {0};
  // XXX extract initialization
  rr.request = &request;
  rr.verb = &verb;
  rr.arg = &arg;
  rr.response = &response;
  
  if (!stralloc_copys(rr.request,"greeting")) die_nomem();

  for (;;) {
    if (rr.request->len) { //XXX && !rr.response->len
      parse_request(rr.request,rr.verb,rr.arg);
      handle_request(from_client,to_server,
                     from_server,to_client,
                     rr.request,rr.verb,rr.arg,
                     &want_data,&in_data);
      // XXX don't do this
      if (!stralloc_copys(rr.request,"")) die_nomem();
    }

    if (rr.response->len) {
      handle_response(to_client,rr.response,
                      rr.verb,rr.arg);
      // XXX reinitialize all of rr here
      if (!stralloc_copys(rr.response,"")) die_nomem();
      if (!stralloc_copys(rr.verb,"")) die_nomem();
      if (!stralloc_copys(rr.arg,"")) die_nomem();
    }

    FD_ZERO(&fds);
    want_to_read(from_client);
    want_to_read(from_server);

    if (!can_read_something(from_client,from_server)) continue;

    if (can_read(from_client)) {
      if (!safeappend(&partial_request,from_client,buf,sizeof buf)) break;
      if (is_entire_line(&partial_request)) {
        if (!stralloc_copy(rr.request,&partial_request)) die_nomem();
        if (!stralloc_copys(&partial_request,"")) die_nomem();
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&partial_response,from_server,buf,sizeof buf)) break;
      if (is_entire_line(&partial_response)) {
        if (!stralloc_copy(rr.response,&partial_response)) die_nomem();
        if (!stralloc_copys(&partial_response,"")) die_nomem();
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

void be_parent(int from_client,int to_client,
               int from_proxy,int to_proxy,
               int from_server,int to_server,
               int child) {
  setup_proxy(from_proxy,to_proxy);
  do_proxy_stuff(from_client,to_server,from_server,to_client);

  teardown_proxy_and_exit(child,from_server,to_server);
}

int main(int argc,char **argv) {
  int from_client;
  int from_proxy, to_server;
  int from_server, to_proxy;
  int to_client;
  int child;

  argv += 1; if (!*argv) die_usage();

  from_client = 0;
  mypipe(&from_proxy, &to_server);
  mypipe(&from_server, &to_proxy);
  to_client = 1;

  if ((child = fork()))
    be_parent(from_client,to_client,
              from_proxy,to_proxy,
              from_server,to_server,
              child);
  else if (child == 0)
    be_child(from_proxy,to_proxy,
             from_server,to_server,
             argv);
  else
    die_fork();
}

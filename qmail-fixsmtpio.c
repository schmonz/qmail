#include "case.h"
#include "env.h"
#include "error.h"
#include "fd.h"
#include "fmt.h"
#include "readwrite.h"
#include "select.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

#define GREETING_PSEUDOVERB "greeting"
#define HOMEPAGE "https://schmonz.com/qmail/authutils"
#define PIPE_READ_BUFFER_SIZE SUBSTDIO_INSIZE

void die() { _exit(1); }

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void dieerrflush(char *s) {
  substdio_putsflush(&sserr,"qmail-fixsmtpio: ");
  substdio_putsflush(&sserr,s);
  substdio_putsflush(&sserr,"\n");
  die();
}

void die_usage() { dieerrflush("usage: qmail-fixsmtpio prog [ arg ... ]"); }
void die_pipe()  { dieerrflush("unable to open pipe"); }
void die_fork()  { dieerrflush("unable to fork"); }
void die_read()  { dieerrflush("unable to read"); }
void die_write() { dieerrflush("unable to write"); }
void die_nomem() { dieerrflush("out of memory"); }

struct request_response {
  stralloc *client_request;
  stralloc *client_verb;
  stralloc *client_arg;
  stralloc *proxy_request;
  stralloc *server_response;
  stralloc *proxy_response;
};

int exitcode = 0;

void cat(stralloc *to,stralloc *from) {
  if (!stralloc_cat(to,from)) die_nomem();
}

void cats(stralloc *to,char *from) {
  if (!stralloc_cats(to,from)) die_nomem();
}

void copys(stralloc *to,char *from) {
  if (!stralloc_copys(to,from)) die_nomem();
}

void blank(stralloc *sa) {
  copys(sa,"");
}

void copy(stralloc *to,stralloc *from) {
  if (!stralloc_copy(to,from)) die_nomem();
}

void strip_last_eol(stralloc *sa) {
  if (sa->len > 0 && sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->len > 0 && sa->s[sa->len-1] == '\r') sa->len--;
}

int accepted_data(stralloc *server_response) {
  return stralloc_starts(server_response,"354 ");
}

void munge_timeout(stralloc *proxy_response) {
  exitcode = 16;
}

void munge_greeting(stralloc *proxy_response) {
  char *x;
  char uid[FMT_ULONG];

  if (stralloc_starts(proxy_response,"4")) exitcode = 14;
  else if (stralloc_starts(proxy_response,"5")) exitcode = 15;
  else {
    copys(proxy_response,"235 ok");
    x = env_get("AUTHUSER");
    if (x) {
      cats(proxy_response,", ");
      cats(proxy_response,x);
      cats(proxy_response,",");
    }
    cats(proxy_response," go ahead ");
    str_copy(uid + fmt_ulong(uid,getuid()),"");
    cats(proxy_response,uid);
    cats(proxy_response," (#2.0.0)\r\n");
  }
}

void munge_help(stralloc *proxy_response) {
  stralloc munged = {0};
  copys(&munged,"214 qmail-fixsmtpio home page: ");
  cats(&munged, HOMEPAGE);
  cats(&munged, "\r\n");
  cat(&munged,proxy_response);
  copy(proxy_response,&munged);
}

void munge_test(stralloc *proxy_response) {
  strip_last_eol(proxy_response);
  cats(proxy_response," and also it's mungeable\r\n");
}

void munge_ehlo(stralloc *proxy_response) {
  stralloc munged = {0};
  stralloc line = {0};
  stralloc subline = {0};

  char *avoids[] = {
    "AUTH ",
    0,
  };

  for (int i = 0; i < proxy_response->len; i++) {
    if (!stralloc_append(&line,i + proxy_response->s)) die_nomem();
    if (proxy_response->s[i] == '\n' || i == proxy_response->len - 1) {
      if (!stralloc_copyb(&subline,line.s + 4,line.len - 4)) die_nomem();
      int keep = 1;
      char *s;
      for (int j = 0; (s = avoids[j]); j++)
        if (stralloc_starts(&line,"250"))
          if (stralloc_starts(&subline,s))
            keep = 0;
      if (keep) cat(&munged,&line);
      blank(&line);
      blank(&subline);
    }
  }
  copy(proxy_response,&munged);
}

int verb_matches(char *s,stralloc *sa) {
  if (!sa->len) return 0;
  return !case_diffb(s,sa->len,sa->s);
}

void change_every_line_fourth_char_to_dash(stralloc *multiline) {
  int pos = 0;
  for (int i = 0; i < multiline->len; i++) {
    if (multiline->s[i] == '\n') pos = -1;
    if (pos == 3) multiline->s[i] = '-';
    pos++;
  }
}

void change_last_line_fourth_char_to_space(stralloc *multiline) {
  int pos = 0;
  for (int i = multiline->len - 2; i >= 0; i--) {
    if (multiline->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  multiline->s[pos+3] = ' ';
}

void reformat_multiline_response(stralloc *proxy_response) {
  change_every_line_fourth_char_to_dash(proxy_response);
  change_last_line_fourth_char_to_space(proxy_response);
}

void munge_response(stralloc *proxy_response,stralloc *verb) {
  if (verb_matches(GREETING_PSEUDOVERB,verb)) munge_greeting(proxy_response);
  if (verb_matches("help",verb)) munge_help(proxy_response);
  if (verb_matches("test",verb)) munge_test(proxy_response);
  if (verb_matches("ehlo",verb)) munge_ehlo(proxy_response);
  reformat_multiline_response(proxy_response);
}

int is_entire_line(stralloc *sa) {
  return sa->len > 0 && sa->s[sa->len - 1] == '\n';
}

int could_be_final_response_line(stralloc *line) {
  return line->len >= 4 && line->s[3] == ' ';
}

int is_entire_response(stralloc *server_response) {
  stralloc lastline = {0};
  int pos = 0;
  if (!is_entire_line(server_response)) return 0;
  for (int i = server_response->len - 2; i >= 0; i--) {
    if (server_response->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  if (!stralloc_copyb(&lastline,server_response->s+pos,server_response->len-pos)) die_nomem();
  return could_be_final_response_line(&lastline);
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

void parse_client_request(stralloc *verb,stralloc *arg,stralloc *request) {
  int i;
  for (i = 0; i < request->len; i++)
    if (request->s[i] == ' ') break;
  i++;

  if (i > request->len) {
    copy(verb,request);
    blank(arg);
  } else {
    if (!stralloc_copyb(verb,request->s,i-1)) die_nomem();
    if (!stralloc_copyb(arg,request->s+i,request->len-i)) die_nomem();
  }
  strip_last_eol(verb);
  strip_last_eol(arg);
}

void logit(char logprefix,stralloc *sa) {
  substdio_putflush(&sserr,&logprefix,1);
  substdio_putsflush(&sserr,": ");
  substdio_putflush(&sserr,sa->s,sa->len);
  if (!is_entire_line(sa)) substdio_putsflush(&sserr,"\r\n");
}

void safewrite(int fd,stralloc *sa) {
  if (write(fd,sa->s,sa->len) == -1) die_write();
}

char *smtp_test(stralloc *client_verb,stralloc *client_arg) {
  static stralloc proxy_response = {0};
  copys(&proxy_response,"250 qmail-fixsmtpio test ok: ");
  if (!stralloc_catb(&proxy_response,client_arg->s,client_arg->len)) die_nomem();
  cats(&proxy_response,"\r\n");
  if (!stralloc_0(&proxy_response)) die_nomem();
  return proxy_response.s;
}

char *smtp_unimplemented(stralloc *client_verb,stralloc *client_arg) {
  static stralloc proxy_response = {0};
  copys(&proxy_response,"502 unimplemented (#5.5.1)");
  cats(&proxy_response,"\r\n");
  if (!stralloc_0(&proxy_response)) die_nomem();
  return proxy_response.s;
}

struct internal_verb {
  char *name;
  char *(*func)();
};

struct internal_verb verbs[] = {
  { "test", smtp_test }
, { "auth", smtp_unimplemented }
, { "starttls", smtp_unimplemented }
, { 0, 0 }
};

void *handle_internally(stralloc *verb,stralloc *arg) {
  for (int i = 0; verbs[i].name; ++i)
    if (verb_matches(verbs[i].name,verb))
      return verbs[i].func;
  return 0;
}

void construct_proxy_request(stralloc *proxy_request,
                             stralloc *verb,stralloc *arg,
                             stralloc *client_request,
                             int *want_data,int *in_data) {
  if (*in_data) {
    copy(proxy_request,client_request);
    if (is_last_line_of_data(proxy_request)) *in_data = 0;
  } else {
    if (handle_internally(verb,arg)) {
      copys(proxy_request,"NOOP qmail-fixsmtpio ");
      cat(proxy_request,client_request);
    } else {
      if (verb_matches("data",verb)) *want_data = 1;
      copy(proxy_request,client_request);
    }
  }
}

void construct_proxy_response(stralloc *proxy_response,
                              stralloc *verb,stralloc *arg,
                              stralloc *server_response,
                              int request_received,
                              int *want_data,int *in_data) {
  char *(*func)();

  if (*want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) *in_data = 1;
  }
  if ((func = handle_internally(verb,arg))) {
    copys(proxy_response,func(verb,arg));
  } else {
    copy(proxy_response,server_response);
  }
  munge_response(proxy_response,verb);
  if (!verb->len && !request_received) munge_timeout(proxy_response);
}

void request_response_init(struct request_response *rr) {
  static stralloc client_request = {0},
                  client_verb = {0},
                  client_arg = {0},
                  proxy_request = {0},
                  server_response = {0},
                  proxy_response = {0};

  blank(&client_request);
  blank(&client_verb);
  blank(&client_arg);
  blank(&proxy_request);
  blank(&server_response);
  blank(&proxy_response);

  rr->client_request = &client_request;
  rr->client_verb = &client_verb;
  rr->client_arg = &client_arg;
  rr->proxy_request = &proxy_request;
  rr->server_response = &server_response;
  rr->proxy_response = &proxy_response;
}

void do_proxy_stuff(int from_client,int to_server,
                    int from_server,int to_client) {
  char buf[PIPE_READ_BUFFER_SIZE];
  int want_data = 0, in_data = 0;
  stralloc partial_request = {0}, partial_response = {0};
  struct request_response rr;

  request_response_init(&rr);
  copys(rr.client_verb,GREETING_PSEUDOVERB);

  for (;;) {
    if (rr.client_request->len && !rr.proxy_request->len) {
      logit('1',rr.client_request);
      if (!in_data)
        parse_client_request(rr.client_verb,rr.client_arg,rr.client_request);
      logit('2',rr.client_verb);
      logit('3',rr.client_arg);
      construct_proxy_request(rr.proxy_request,
                              rr.client_verb,rr.client_arg,
                              rr.client_request,
                              &want_data,&in_data);
      logit('4',rr.proxy_request);
      safewrite(to_server,rr.proxy_request);
      if (in_data) {
        blank(rr.client_request);
        blank(rr.proxy_request);
      }
    }

    if (rr.server_response->len && !rr.proxy_response->len) {
      logit('5',rr.server_response);
      construct_proxy_response(rr.proxy_response,
                               rr.client_verb,rr.client_arg,
                               rr.server_response,
                               rr.client_request->len,
                               &want_data,&in_data);
      logit('6',rr.proxy_response);
      safewrite(to_client,rr.proxy_response);
      request_response_init(&rr);
    }

    FD_ZERO(&fds);
    want_to_read(from_client);
    want_to_read(from_server);

    if (!can_read_something(from_client,from_server)) continue;

    if (can_read(from_client)) {
      if (!safeappend(&partial_request,from_client,buf,sizeof buf)) break;
      if (is_entire_line(&partial_request)) {
        copy(rr.client_request,&partial_request);
        blank(&partial_request);
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&partial_response,from_server,buf,sizeof buf)) break;
      if (is_entire_response(&partial_response)) {
        copy(rr.server_response,&partial_response);
        blank(&partial_response);
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

  _exit(exitcode);
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
  mypipe(&from_proxy,&to_server);
  mypipe(&from_server,&to_proxy);
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

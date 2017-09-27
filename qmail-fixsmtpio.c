#include "case.h"
#include "env.h"
#include "error.h"
#include "fd.h"
#include "fmt.h"
#include "getln.h"
#include "readwrite.h"
#include "select.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

#define GREETING_PSEUDOREQUEST "greeting"
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
  stralloc *verb; stralloc *arg;
  stralloc *proxy_request;
  stralloc *server_response;
  stralloc *proxy_response;
};

int exitcode = 0;

void blank(stralloc *sa) {
  if (!stralloc_copys(sa,"")) die_nomem();
}

void strip_last_eol(stralloc *sa) {
  if (sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->s[sa->len-1] == '\r') sa->len--;
}

int accepted_data(stralloc *server_response) {
  return stralloc_starts(server_response,"354 ");
}

void munge_timeout(stralloc *server_response) {
  exitcode = 16;
}

void munge_greeting(stralloc *server_response) {
  char *x;
  char uid[FMT_ULONG];

  if (stralloc_starts(server_response,"4")) exitcode = 14;
  else if (stralloc_starts(server_response,"5")) exitcode = 15;
  else {
    if (!stralloc_copys(server_response,"235 ok")) die_nomem();
    x = env_get("AUTHUSER");
    if (x) {
      if (!stralloc_cats(server_response,", ")) die_nomem();
      if (!stralloc_cats(server_response,x)) die_nomem();
      if (!stralloc_cats(server_response,",")) die_nomem();
    }
    if (!stralloc_cats(server_response," go ahead ")) die_nomem();
    str_copy(uid + fmt_ulong(uid,getuid()),"");
    if (!stralloc_cats(server_response,uid)) die_nomem();
    if (!stralloc_cats(server_response," (#2.0.0)")) die_nomem();
  }
}

void munge_help(stralloc *server_response) {
  stralloc munged = {0};
  if (!stralloc_copys(&munged,"214 qmail-fixsmtpio home page: ")) die_nomem();
  if (!stralloc_cats(&munged, HOMEPAGE)) die_nomem();
  if (!stralloc_cats(&munged, "\r\n")) die_nomem();
  if (!stralloc_cat(&munged,server_response)) die_nomem();
  if (!stralloc_copy(server_response,&munged)) die_nomem();
}

void munge_test(stralloc *server_response) {
  if (!stralloc_cats(server_response," and also it's mungeable")) die_nomem();
}

void munge_ehlo(stralloc *server_response) {
  stralloc munged = {0};
  stralloc line = {0};
  stralloc subline = {0};

  char *avoids[] = {
    "AUTH ",
    0,
  };

  for (int i = 0; i < server_response->len; i++) {
    if (!stralloc_append(&line,i + server_response->s)) die_nomem();
    if (server_response->s[i] == '\n' || i == server_response->len - 1) {
      if (!stralloc_copyb(&subline,line.s + 4,line.len - 4)) die_nomem();
      int keep = 1;
      char *s;
      for (int j = 0; (s = avoids[j]); j++)
        if (stralloc_starts(&line,"250"))
          if (stralloc_starts(&subline,s))
            keep = 0;
      if (keep && !stralloc_cat(&munged,&line)) die_nomem();
      blank(&line);
      blank(&subline);
    }
  }
  strip_last_eol(&munged);
  if (!stralloc_copy(server_response,&munged)) die_nomem();
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
  for (int i = multiline->len - 1; i >= 0; i--) {
    if (multiline->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  multiline->s[pos+3] = ' ';
}

void reformat_multiline_response(stralloc *server_response) {
  change_every_line_fourth_char_to_dash(server_response);
  change_last_line_fourth_char_to_space(server_response);
  if (!stralloc_cats(server_response,"\r\n")) die_nomem();
}

void munge_response(stralloc *server_response,struct request_response *rr) {
  strip_last_eol(server_response);
  if (verb_matches(GREETING_PSEUDOREQUEST,rr->verb)) munge_greeting(server_response);
  if (verb_matches("help",rr->verb)) munge_help(server_response);
  if (verb_matches("test",rr->verb)) munge_test(server_response);
  if (verb_matches("ehlo",rr->verb)) munge_ehlo(server_response);
  reformat_multiline_response(server_response);
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

void parse_client_request(struct request_response *rr) {
  // XXX do not mess with client_request
  // XXX use it to populate verb and arg, then get out of here
  // XXX then call generate_proxy_request()
  stralloc chomped = {0};
  int len;
  int first_space;

  len = rr->client_request->len;

  if (!stralloc_0(rr->client_request)) die_nomem();
  first_space = str_chr(rr->client_request->s,' ');

  // XXX strip_last_eol();
  if (rr->client_request->s[len-1] == '\n') len--;
  if (rr->client_request->s[len-1] == '\r') len--;
  if (!stralloc_copyb(&chomped,rr->client_request->s,len)) die_nomem();

  if (first_space <= 0 || first_space >= chomped.len) {
    if (!stralloc_copy(rr->verb,&chomped)) die_nomem();
    blank(rr->arg);
  } else {
    if (!stralloc_copyb(rr->verb,chomped.s,first_space)) die_nomem();
    if (!stralloc_copyb(rr->arg,chomped.s + first_space + 1,chomped.len - first_space - 1)) die_nomem();
  }

  if (!stralloc_copy(rr->client_request,&chomped)) die_nomem();
  if (!stralloc_cats(rr->client_request,"\r\n")) die_nomem();
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
  stralloc proxy_response = {0};
  if (!stralloc_copys(&proxy_response,"250 qmail-fixsmtpio test ok: ")) die_nomem();
  if (!stralloc_catb(&proxy_response,arg->s,arg->len)) die_nomem();
  if (!stralloc_cats(&proxy_response,"\r\n")) die_nomem();
  if (!stralloc_0(&proxy_response)) die_nomem();
  return proxy_response.s;
}

char *smtp_unimplemented(stralloc *verb,stralloc *arg) {
  stralloc proxy_response = {0};
  if (!stralloc_copys(&proxy_response,"502 unimplemented (#5.5.1)")) die_nomem();
  if (!stralloc_cats(&proxy_response,"\r\n")) die_nomem();
  if (!stralloc_0(&proxy_response)) die_nomem();
  return proxy_response.s;
}

char *handle_internally(stralloc *verb,stralloc *arg) {
  if (verb_matches("noop",verb)) return 0;
  if (verb_matches("test",verb)) return smtp_test(verb,arg);
  if (verb_matches("auth",verb)) return smtp_unimplemented(verb,arg);
  if (verb_matches("starttls",verb)) return smtp_unimplemented(verb,arg);

  return 0;
}

void send_keepalive(int server,stralloc *client_request) {
  stralloc keepalive = {0};
  if (!stralloc_copys(&keepalive,"NOOP qmail-fixsmtpio ")) die_nomem();
  if (!stralloc_cat(&keepalive,client_request)) die_nomem();
  write_to_server(server,&keepalive);
}

void check_keepalive(int client,stralloc *server_response) {
  if (!stralloc_starts(server_response,"250 ok")) {
    write_to_client(client,server_response);
    die();
  }
}

char *blocking_line_read(int fd) {
  char buf[SUBSTDIO_INSIZE];
  substdio ss;
  stralloc line = {0};
  int match;

  substdio_fdbuf(&ss,saferead,fd,buf,sizeof buf);
  if (getln(&ss,&line,&match,'\n') == -1) die_nomem();
  if (!stralloc_0(&line)) die_nomem();
  return line.s;
}

void handle_request(int from_client,int to_server,
                    int from_server,int to_client,
                    struct request_response *rr,
                    int *want_data,int *in_data) {
  char *internal_response;
  stralloc sa_internal_response = {0};
  stralloc sa_keepalive_response = {0};

  if (*in_data) {
    write_to_server(to_server,rr->client_request);
    if (is_last_line_of_data(rr->client_request)) *in_data = 0;
  } else {
    if ((internal_response = handle_internally(rr->verb,rr->arg))) {
      send_keepalive(to_server,rr->client_request);
      if (!stralloc_copys(&sa_keepalive_response,blocking_line_read(from_server))) die_nomem();
      check_keepalive(to_client,&sa_keepalive_response);
      logit('O',&sa_keepalive_response);

      logit('I',rr->client_request);
      if (!stralloc_copys(&sa_internal_response,internal_response)) die_nomem();
      munge_response(&sa_internal_response,rr);
      write_to_client(to_client,&sa_internal_response);
      blank(rr->verb);
      blank(rr->arg);
    } else {
      if (verb_matches("data",rr->verb)) *want_data = 1;
      write_to_server(to_server,rr->client_request);
    }
  }
}

void handle_response(int to_client,
                     struct request_response *rr,
                     int *want_data,int *in_data) {
  if (*want_data) {
    *want_data = 0;
    if (accepted_data(rr->server_response)) *in_data = 1;
  }
  munge_response(rr->server_response,rr);
  write_to_client(to_client,rr->server_response);
}

void request_response_init(struct request_response *rr) {
  static stralloc client_request = {0},
                  verb = {0}, arg = {0},
                  proxy_request = {0},
                  server_response = {0},
                  proxy_response = {0};

  blank(&client_request);
  blank(&verb); blank(&arg);
  blank(&proxy_request);
  blank(&server_response);
  blank(&proxy_response);

  rr->client_request = &client_request;
  rr->verb = &verb; rr->arg = &arg;
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
  if (!stralloc_copys(rr.client_request,GREETING_PSEUDOREQUEST)) die_nomem();

  for (;;) {
    if (rr.client_request->len && !rr.server_response->len) {
      parse_client_request(&rr);
      handle_request(from_client,to_server,
                     from_server,to_client,
                     &rr,
                     &want_data,&in_data);
    }

    if (rr.server_response->len) {
      handle_response(to_client,
                      &rr,
                      &want_data,&in_data);
      request_response_init(&rr);
    }

    FD_ZERO(&fds);
    want_to_read(from_client);
    want_to_read(from_server);

    if (!can_read_something(from_client,from_server)) continue;

    if (can_read(from_client)) {
      if (!safeappend(&partial_request,from_client,buf,sizeof buf)) break;
      if (is_entire_line(&partial_request)) {
        if (!stralloc_copy(rr.client_request,&partial_request)) die_nomem();
        blank(&partial_request);
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&partial_response,from_server,buf,sizeof buf)) break;
      if (is_entire_response(&partial_response)) {
        if (!stralloc_copy(rr.server_response,&partial_response)) die_nomem();
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

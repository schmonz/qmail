#include <fnmatch.h>
#include "alloc.h"
#include "auto_qmail.h"
#include "case.h"
#include "control.h"
#include "env.h"
#include "error.h"
#include "fd.h"
#include "readwrite.h"
#include "scan.h"
#include "select.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

#define GREETING_PSEUDOVERB "greeting"
#define TIMEOUT_PSEUDOVERB "timeout"
#define CLIENTEOF_PSEUDOVERB "clienteof"
#define MODIFY_INTERNALLY "&qmail-fixsmtpio"
#define AUTHUSER "AUTHUSER"
#define HOMEPAGE "https://schmonz.com/qmail/authutils"
#define PIPE_READ_SIZE SUBSTDIO_INSIZE
#define USE_CHILD_EXITCODE -1

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
void die_control(){dieerrflush("unable to read controls"); }
void die_format(){ dieerrflush("unable to parse controls"); }
void die_pipe()  { dieerrflush("unable to open pipe"); }
void die_fork()  { dieerrflush("unable to fork"); }
void die_exec()  { dieerrflush("unable to exec"); }
void die_wait()  { dieerrflush("unable to wait for child"); }
void die_crash() { dieerrflush("aack, child crashed"); }
void die_read()  { dieerrflush("unable to read"); }
void die_write() { dieerrflush("unable to write"); }
void die_nomem() { dieerrflush("out of memory"); }

typedef struct request_response {
  stralloc *client_request;
  stralloc *client_verb;
  stralloc *client_arg;
  stralloc *proxy_request;
  stralloc *server_response;
  stralloc *proxy_response;
} request_response;

typedef struct filter_rule {
  struct filter_rule *next;
  char *env;
  char *event;
  char *request_prepend;
  char *response_line_glob;
  int exitcode;
  char *response;
} filter_rule;

filter_rule *add_rule(filter_rule *next,
                      char *env,char *event,char *request_prepend,
                      char *response_line_glob,int exitcode,char *response) {
  filter_rule *fr = (filter_rule *)alloc(sizeof(filter_rule));
  if (!fr) die_nomem();
  fr->next = next;
  fr->env = env; fr->event = event; fr->request_prepend = request_prepend;
  fr->response_line_glob = response_line_glob; fr->exitcode = exitcode;
  fr->response = response;
  next = fr;
  return next;
}

stralloc smtpgreeting = {0};
int exitcode = USE_CHILD_EXITCODE;

void cat(stralloc *to,stralloc *from) {
  if (!stralloc_cat(to,from)) die_nomem();
}

void catb(stralloc *to,char *buf,int len) {
  if (!stralloc_catb(to,buf,len)) die_nomem();
}

void cats(stralloc *to,char *from) {
  if (!stralloc_cats(to,from)) die_nomem();
}

void copy(stralloc *to,stralloc *from) {
  if (!stralloc_copy(to,from)) die_nomem();
}

void copyb(stralloc *to,char *buf,int len) {
  if (!stralloc_copyb(to,buf,len)) die_nomem();
}

void copys(stralloc *to,char *from) {
  if (!stralloc_copys(to,from)) die_nomem();
}

int starts(stralloc *haystack,char *needle) {
  return stralloc_starts(haystack,needle);
}

void blank(stralloc *sa) {
  copys(sa,"");
}

void strip_last_eol(stralloc *sa) {
  if (sa->len > 0 && sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->len > 0 && sa->s[sa->len-1] == '\r') sa->len--;
}

int accepted_data(stralloc *response) {
  return starts(response,"354 ");
}

void munge_greeting(stralloc *response) {
  char *x;

  if ((x = env_get(AUTHUSER))) {
    copys(response,"235 ok, ");
    cats(response,x);
    cats(response,",");
    cats(response," go ahead (#2.0.0)");
  } else {
    copys(response,"220 ");
    cat(response,&smtpgreeting);
  }
}

void munge_helo(stralloc *response) {
  copys(response,"250 ");
  cat(response,&smtpgreeting);
}

void munge_ehlo(stralloc *response,int lineno) {
  if (lineno) return;
  munge_helo(response);
}

void munge_help(stralloc *response) {
  stralloc munged = {0};
  copys(&munged,"214 qmail-fixsmtpio home page: ");
  cats(&munged,HOMEPAGE);
  cats(&munged,"\r\n");
  cat(&munged,response);
  copy(response,&munged);
}

void munge_quit(stralloc *response) {
  copys(response,"221 ");
  cat(response,&smtpgreeting);
}

int verb_matches(char *s,stralloc *sa) {
  if (!sa->len) return 0;
  return !case_diffb(s,sa->len,sa->s);
}

void change_every_line_fourth_char_to_dash(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = 0; i < multiline->len; i++) {
    if (multiline->s[i] == '\n') pos = -1;
    if (pos == 3) multiline->s[i] = '-';
    pos++;
  }
}

void change_last_line_fourth_char_to_space(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = multiline->len - 2; i >= 0; i--) {
    if (multiline->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  multiline->s[pos+3] = ' ';
}

void reformat_multiline_response(stralloc *response) {
  change_every_line_fourth_char_to_dash(response);
  change_last_line_fourth_char_to_space(response);
}

int is_entire_line(stralloc *sa) {
  return sa->len > 0 && sa->s[sa->len - 1] == '\n';
}

void munge_response_line(stralloc *line,int lineno,
                         filter_rule *rules,stralloc *verb) {
  stralloc line0 = {0};
  filter_rule *fr;
  copy(&line0,line);
  if (!stralloc_0(&line0)) die_nomem();

  for (fr = rules; fr; fr = fr->next) {
    if (fr->env && !env_get(fr->env)) continue;
    if (!verb_matches(fr->event,verb)) continue;

    if (0 == fnmatch(fr->response_line_glob,line0.s,0)) {
      if (fr->exitcode != USE_CHILD_EXITCODE) exitcode = fr->exitcode;
      if (!fr->response) continue;
      if (0 == str_diff(MODIFY_INTERNALLY,fr->response)) {
        if (verb_matches(GREETING_PSEUDOVERB,verb)) munge_greeting(line);
        if (verb_matches("ehlo",verb)) munge_ehlo(line,lineno);
        if (verb_matches("helo",verb)) munge_helo(line);
        if (verb_matches("help",verb)) munge_help(line);
        if (verb_matches("quit",verb)) munge_quit(line);
      } else {
        copys(line,fr->response);
      }
    }
  }
  if (line->len) if (!is_entire_line(line)) cats(line,"\r\n");
}

void munge_response(stralloc *response,filter_rule *rules,stralloc *verb) {
  stralloc munged = {0};
  stralloc line = {0};
  int lineno = 0;
  int i;

  for (i = 0; i < response->len; i++) {
    if (!stralloc_append(&line,i + response->s)) die_nomem();
    if (response->s[i] == '\n' || i == response->len - 1) {
      munge_response_line(&line,lineno++,rules,verb);
      cat(&munged,&line);
      blank(&line);
    }
  }

  reformat_multiline_response(&munged);
  copy(response,&munged);
}

int could_be_final_response_line(stralloc *line) {
  return line->len >= 4 && line->s[3] == ' ';
}

int is_entire_response(stralloc *response) {
  stralloc lastline = {0};
  int pos = 0;
  int i;
  if (!is_entire_line(response)) return 0;
  for (i = response->len - 2; i >= 0; i--) {
    if (response->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  copyb(&lastline,response->s+pos,response->len-pos);
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

void setup_child(int from_proxy,int to_server,
                 int from_server,int to_proxy) {
  close(from_server);
  close(to_server);
  use_as_stdin(from_proxy);
  use_as_stdout(to_proxy);
}

void exec_child_and_never_return(char **argv) {
  execvp(*argv,argv);
  die_exec();
}

void be_child(int from_proxy,int to_proxy,
              int from_server,int to_server,
              char **argv) {
  setup_child(from_proxy,to_server,from_server,to_proxy);
  exec_child_and_never_return(argv);
}

void setup_parent(int from_proxy,int to_proxy) {
  close(from_proxy);
  close(to_proxy);
}

fd_set fds;

void want_to_read(int fd1,int fd2) {
  FD_ZERO(&fds);
  FD_SET(fd1,&fds);
  FD_SET(fd2,&fds);
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
  catb(sa,buf,r);
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
    copyb(verb,request->s,i-1);
    copyb(arg,request->s+i,request->len-i);
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

void construct_proxy_request(stralloc *proxy_request,
                             filter_rule *rules,
                             stralloc *verb,stralloc *arg,
                             stralloc *client_request,
                             int *want_data,int *in_data) {
  filter_rule *fr;

  if (*in_data) {
    copy(proxy_request,client_request);
    if (is_last_line_of_data(proxy_request)) *in_data = 0;
  } else {
    for (fr = rules; fr; fr = fr->next)
      if (fr->request_prepend && verb_matches(fr->event,verb))
        if ((!fr->env) || (fr->env && env_get(fr->env)))
          cats(proxy_request,fr->request_prepend);
    if (verb_matches("data",verb)) *want_data = 1;
    cat(proxy_request,client_request);
  }
}

void construct_proxy_response(stralloc *proxy_response,
                              filter_rule *rules,
                              stralloc *verb,stralloc *arg,
                              stralloc *server_response,
                              int request_received,
                              int *want_data,int *in_data) {
  if (*want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) *in_data = 1;
  }
  copy(proxy_response,server_response);
  if (!request_received && !verb->len) copys(verb,TIMEOUT_PSEUDOVERB);
  munge_response(proxy_response,rules,verb);
}

void request_response_init(request_response *rr) {
  static stralloc client_request = {0},
                  client_verb = {0},
                  client_arg = {0},
                  proxy_request = {0},
                  server_response = {0},
                  proxy_response = {0};

  blank(&client_request); rr->client_request = &client_request;
  blank(&client_verb); rr->client_verb = &client_verb;
  blank(&client_arg); rr->client_arg = &client_arg;
  blank(&proxy_request); rr->proxy_request = &proxy_request;
  blank(&server_response); rr->server_response = &server_response;
  blank(&proxy_response); rr->proxy_response = &proxy_response;
}

void handle_client_request(int to_server,filter_rule *rules,
                           request_response *rr,
                           int *want_data,int *in_data) {
  logit('1',rr->client_request);
  if (!*in_data)
    parse_client_request(rr->client_verb,rr->client_arg,rr->client_request);
  logit('2',rr->client_verb);
  logit('3',rr->client_arg);
  construct_proxy_request(rr->proxy_request,rules,
                          rr->client_verb,rr->client_arg,
                          rr->client_request,
                          want_data,in_data);
  logit('4',rr->proxy_request);
  safewrite(to_server,rr->proxy_request);
  if (*in_data) {
    blank(rr->client_request);
    blank(rr->proxy_request);
  }
}

void handle_server_response(int to_client,filter_rule *rules,
                            request_response *rr,
                            int *want_data,int *in_data) {
  logit('5',rr->server_response);
  construct_proxy_response(rr->proxy_response,rules,
                           rr->client_verb,rr->client_arg,
                           rr->server_response,
                           rr->client_request->len,
                           want_data,in_data);
  logit('6',rr->proxy_response);
  safewrite(to_client,rr->proxy_response);
  request_response_init(rr);
}

int request_needs_handling(request_response *rr) {
  return rr->client_request->len && !rr->proxy_request->len;
}

int response_needs_handling(request_response *rr) {
  return rr->server_response->len && !rr->proxy_response->len;
}

void prepare_for_handling(stralloc *to,stralloc *from) {
  copy(to,from);
  blank(from);
}

char *get_next_field(int *start,stralloc *line) {
  stralloc temp = {0};
  int i;
  for (i = *start; i < line->len; i++) {
    if (!stralloc_append(&temp,i + line->s)) die_nomem();
    if (line->s[i] == ':' || i == line->len - 1) {
      *start = i + 1;
      temp.s[temp.len - 1] = '\0';
      return temp.s;
    }
  }
  return 0;
}

filter_rule *load_filter_rule(filter_rule *rules,stralloc *line) {
  int pos = 0;
  char *env                = get_next_field(&pos,line);
  char *event              = get_next_field(&pos,line);
  char *request_prepend    = get_next_field(&pos,line);
  char *response_line_glob = get_next_field(&pos,line);
  char *exitcode_str       = get_next_field(&pos,line);
  char *response           = get_next_field(&pos,line);
  int exitcode;

  if (0 == str_len(env))                env = 0;
  if (0 == str_len(event))              die_format();
  if (0 == str_len(request_prepend))    request_prepend = 0;
  if (0 == str_len(response_line_glob)) die_format();
  if (0 == str_len(exitcode_str))       exitcode = USE_CHILD_EXITCODE;
  else if (!scan_ulong(exitcode_str,&exitcode)) die_format();
  if (!response || 0 == str_len(response))
    ;

  return add_rule(rules,env,event,request_prepend,response_line_glob,exitcode,response);
}

filter_rule *load_filter_rules() {
  stralloc lines = {0}, line = {0};
  filter_rule *rules = 0;
  int i;

  if (chdir(auto_qmail) == -1) die_control();
  if (control_init() == -1) die_control();
  if (control_rldef(&smtpgreeting,"control/smtpgreeting",1,(char *) 0) != 1)
    die_control();
  switch (control_readfile(&lines,"control/fixsmtpio",0)) {
    case -1: die_control();
    case  0: return rules;
  }

  for (i = 0; i < lines.len; i++) {
    if (!stralloc_append(&line,i + lines.s)) die_nomem();
    if (lines.s[i] == '\0' || i == lines.len - 1) {
      rules = load_filter_rule(rules,&line);
      blank(&line);
    }
  }

  return rules;
}

void read_and_process_until_either_end_closes(int from_client,int to_server,
                                              int from_server,int to_client,
                                              filter_rule *rules) {
  char buf[PIPE_READ_SIZE];
  int want_data = 0, in_data = 0;
  stralloc partial_request = {0}, partial_response = {0};
  stralloc client_eof = {0};
  request_response rr;

  copys(&client_eof,CLIENTEOF_PSEUDOVERB);
  request_response_init(&rr);
  copys(rr.client_verb,GREETING_PSEUDOVERB);

  for (;;) {
    if (request_needs_handling(&rr))
      handle_client_request(to_server,rules,&rr,&want_data,&in_data);

    if (response_needs_handling(&rr))
      handle_server_response(to_client,rules,&rr,&want_data,&in_data);

    if (exitcode != USE_CHILD_EXITCODE) break;

    want_to_read(from_client,from_server);
    if (!can_read_something(from_client,from_server)) continue;

    if (can_read(from_client)) {
      if (!safeappend(&partial_request,from_client,buf,sizeof buf)) {
        munge_response_line(&partial_request,0,rules,&client_eof);
        break;
      }
      if (is_entire_line(&partial_request))
        prepare_for_handling(rr.client_request,&partial_request);
    }

    if (can_read(from_server)) {
      if (!safeappend(&partial_response,from_server,buf,sizeof buf)) break;
      if (is_entire_response(&partial_response))
        prepare_for_handling(rr.server_response,&partial_response);
    }
  }
}

void teardown_and_exit(int child,int from_server,int to_server) {
  int wstat;

  close(from_server);
  close(to_server);

  if (wait_pid(&wstat,child) == -1) die_wait();
  if (wait_crashed(wstat)) die_crash();

  if (exitcode == USE_CHILD_EXITCODE) _exit(wait_exitcode(wstat));
  else _exit(exitcode);
}

void be_parent(int from_client,int to_client,
               int from_proxy,int to_proxy,
               int from_server,int to_server,
               int child,filter_rule *rules) {
  setup_parent(from_proxy,to_proxy);
  read_and_process_until_either_end_closes(from_client,to_server,
                                           from_server,to_client,
                                           rules);
  teardown_and_exit(child,from_server,to_server);
}

int main(int argc,char **argv) {
  int from_client;
  int from_proxy, to_server;
  int from_server, to_proxy;
  int to_client;
  int child;

  argv++; if (!*argv) die_usage();

  from_client = 0;
  mypipe(&from_proxy,&to_server);
  mypipe(&from_server,&to_proxy);
  to_client = 1;

  if ((child = fork()))
    be_parent(from_client,to_client,
              from_proxy,to_proxy,
              from_server,to_server,
              child,load_filter_rules());
  else if (child == 0)
    be_child(from_proxy,to_proxy,
             from_server,to_server,
             argv);
  else
    die_fork();
}

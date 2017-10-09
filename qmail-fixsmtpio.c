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

#define HOMEPAGE             "https://schmonz.com/qmail/acceptutils"
#define PROGNAME             "qmail-fixsmtpio"

#define MUNGE_INTERNALLY     "&" PROGNAME
#define PSEUDOVERB_GREETING  "greeting"
#define PSEUDOVERB_TIMEOUT   "timeout"
#define PSEUDOVERB_CLIENTEOF "clienteof"

#define USE_CHILD_EXITCODE_LATER -1

#define PIPE_READ_SIZE       SUBSTDIO_INSIZE

void die() { _exit(1); }

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_putsflush(&sserr,PROGNAME ": ");
  substdio_putsflush(&sserr,s);
  substdio_putsflush(&sserr,"\n");
}

void dieerrflush(char *s) { errflush(s); die(); }

/*
void die_format(stralloc *line,char *s) {
  errflush("unable to parse control/fixsmtpio: ");
  substdio_putsflush(&sserr,s);
  substdio_putsflush(&sserr,": ");
  substdio_putflush(&sserr,line->s,line->len);
  substdio_putsflush(&sserr,"\n");
}
*/

void die_usage() { dieerrflush("usage: " PROGNAME " prog [ arg ... ]"); }
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
  int       proxy_exitcode;
} request_response;

typedef struct filter_rule {
  struct filter_rule *next;
  char *env;
  char *event;
  char *request_prepend;
  char *response_line_glob;
  int   exitcode;
  char *response;
} filter_rule;

filter_rule *prepend_rule(filter_rule *next,
                          char *env,char *event,char *request_prepend,
                          char *response_line_glob,int exitcode,char *response) {
  filter_rule *rule;
  if (!(rule = (filter_rule *)alloc(sizeof(filter_rule)))) die_nomem();
  rule->next               = next;
  rule->env                = env;
  rule->event              = event;
  rule->request_prepend    = request_prepend;
  rule->response_line_glob = response_line_glob;
  rule->exitcode           = exitcode;
  rule->response           = response;
  next = rule;
  return next;
}

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

void munge_greeting(stralloc *response,int lineno,stralloc *greeting) {
  copys(response,"220 "); cat(response,greeting);
}

void munge_helo(stralloc *response,int lineno,stralloc *greeting) {
  copys(response,"250 "); cat(response,greeting);
}

void munge_ehlo(stralloc *response,int lineno,stralloc *greeting) {
  if (lineno) return; munge_helo(response,lineno,greeting);
}

void munge_help(stralloc *response,int lineno,stralloc *greeting) {
  stralloc munged = {0};
  copys(&munged,"214 " PROGNAME " home page: " HOMEPAGE "\r\n");
  cat(&munged,response);
  copy(response,&munged);
}

void munge_quit(stralloc *response,int lineno,stralloc *greeting) {
  copys(response,"221 "); cat(response,greeting);
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

struct munge_command {
  char *event;
  void (*munger)();
};

struct munge_command m[] = {
  { PSEUDOVERB_GREETING, munge_greeting }
, { "ehlo", munge_ehlo }
, { "helo", munge_helo }
, { "help", munge_help }
, { "quit", munge_quit }
, { 0, 0 }
};

void *munge_line_fn(stralloc *verb) {
  int i;
  for (i = 0; m[i].event; ++i)
    if (verb_matches(m[i].event,verb))
      return m[i].munger;
  return 0;
}

void munge_line_internally(stralloc *line,int lineno,
                           stralloc *greeting,stralloc *verb) {
  void (*munger)() = munge_line_fn(verb);
  if (munger) munger(line,lineno,greeting);
  // XXX else we should have died at parse time. log this shit!
}

int string_matches_glob(char *glob,char *string) {
  return 0 == fnmatch(glob,string,0);
}

int want_munge_internally(char *response) {
  return 0 == str_diff(MUNGE_INTERNALLY,response);
}

int envvar_exists_if_needed(char *envvar) {
  if (envvar && env_get(envvar)) return 1;
  else if (envvar) return 0;
  return 1;
}

int filter_rule_applies(filter_rule *rule,stralloc *verb) {
  return (verb_matches(rule->event,verb) && envvar_exists_if_needed(rule->env));
}

void munge_exitcode(int *exitcode,filter_rule *rule) {
  if (rule->exitcode != USE_CHILD_EXITCODE_LATER) *exitcode = rule->exitcode;
}

void munge_response_line(stralloc *line,int lineno,int *exitcode,
                         stralloc *greeting,filter_rule *rules,stralloc *verb) {
  stralloc line0 = {0};
  filter_rule *rule;

  copy(&line0,line);
  if (!stralloc_0(&line0)) die_nomem();

  for (rule = rules; rule; rule = rule->next) {
    if (!filter_rule_applies(rule,verb)) continue;
    if (!string_matches_glob(rule->response_line_glob,line0.s)) continue;
    munge_exitcode(exitcode,rule);
    if (!rule->response) continue;
    if (want_munge_internally(rule->response))
      munge_line_internally(line,lineno,greeting,verb);
    else
      copys(line,rule->response);
  }
  if (line->len) if (!is_entire_line(line)) cats(line,"\r\n");
}

void munge_response(stralloc *response,int *exitcode,
                    stralloc *greeting,filter_rule *rules,
                    stralloc *verb) {
  stralloc munged = {0};
  stralloc line = {0};
  int lineno = 0;
  int i;

  for (i = 0; i < response->len; i++) {
    if (!stralloc_append(&line,i + response->s)) die_nomem();
    if (response->s[i] == '\n' || i == response->len - 1) {
      munge_response_line(&line,lineno++,exitcode,greeting,rules,verb);
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

void setup_child(int from_me,int to_server,
                 int from_server,int to_me) {
  close(from_server);
  close(to_server);
  use_as_stdin(from_me);
  use_as_stdout(to_me);
}

void exec_child_and_never_return(char **argv) {
  execvp(*argv,argv);
  die_exec();
}

void be_child(int from_me,int to_me,
              int from_server,int to_server,
              char **argv) {
  setup_child(from_me,to_server,from_server,to_me);
  exec_child_and_never_return(argv);
}

void setup_parent(int from_me,int to_me) {
  close(from_me);
  close(to_me);
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
    strip_last_eol(verb);
    blank(arg);
  } else {
    copyb(verb,request->s,i-1);
    copyb(arg,request->s+i,request->len-i);
    strip_last_eol(arg);
  }
}

void logit(char logprefix,stralloc *sa) {
  if (!env_get("FIXSMTPIODEBUG")) return;
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
  filter_rule *rule;

  if (*in_data) {
    copy(proxy_request,client_request);
    if (is_last_line_of_data(proxy_request)) *in_data = 0;
  } else {
    for (rule = rules; rule; rule = rule->next)
      if (rule->request_prepend && filter_rule_applies(rule,verb))
        cats(proxy_request,rule->request_prepend);
    if (verb_matches("data",verb)) *want_data = 1;
    cat(proxy_request,client_request);
  }
}

void construct_proxy_response(stralloc *proxy_response,
                              stralloc *greeting,
                              filter_rule *rules,
                              stralloc *verb,stralloc *arg,
                              stralloc *server_response,
                              int request_received,
                              int *proxy_exitcode,
                              int *want_data,int *in_data) {
  if (*want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) *in_data = 1;
  }
  copy(proxy_response,server_response);
  if (!*in_data && !request_received && !verb->len) copys(verb,PSEUDOVERB_TIMEOUT);
  munge_response(proxy_response,proxy_exitcode,greeting,rules,verb);
}

void request_response_init(request_response *rr) {
  static stralloc client_request  = {0},
                  client_verb     = {0},
                  client_arg      = {0},
                  proxy_request   = {0},
                  server_response = {0},
                  proxy_response  = {0};

  blank(&client_request);  rr->client_request  = &client_request;
  blank(&client_verb);     rr->client_verb     = &client_verb;
  blank(&client_arg);      rr->client_arg      = &client_arg;
  blank(&proxy_request);   rr->proxy_request   = &proxy_request;
  blank(&server_response); rr->server_response = &server_response;
  blank(&proxy_response);  rr->proxy_response  = &proxy_response;
                           rr->proxy_exitcode  = USE_CHILD_EXITCODE_LATER;
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

int handle_server_response(int to_client,
                           stralloc *greeting,filter_rule *rules,
                           request_response *rr,
                           int *want_data,int *in_data) {
  int exitcode;
  logit('5',rr->server_response);
  construct_proxy_response(rr->proxy_response,
                           greeting,rules,
                           rr->client_verb,rr->client_arg,
                           rr->server_response,
                           rr->client_request->len,
                           &rr->proxy_exitcode,
                           want_data,in_data);
  logit('6',rr->proxy_response);
  safewrite(to_client,rr->proxy_response);
  exitcode = rr->proxy_exitcode;
  request_response_init(rr);
  return exitcode;
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
  if (0 == str_len(exitcode_str))       exitcode = USE_CHILD_EXITCODE_LATER;
  else if (!scan_ulong(exitcode_str,&exitcode)) die_format();
  if (!response || 0 == str_len(response))
    ;
  // XXX validating non-null, non-empty response goes here

  return prepend_rule(rules,
                      env,event,request_prepend,
                      response_line_glob,exitcode,response);
}

filter_rule *reverse_backwards_rules_to_match_file_order(filter_rule *rules) {
  filter_rule *rules_in_file_order = 0;
  filter_rule *temp;

  while (rules) {
    temp = rules;
    rules = rules->next;
    temp->next = rules_in_file_order;
    rules_in_file_order = temp;
  }

  return rules_in_file_order;
}

filter_rule *load_filter_rules(char *configfile) {
  stralloc lines = {0}, line = {0};
  filter_rule *backwards_rules = 0;
  int i;

  if (control_readfile(&lines,configfile,0) == -1) die_control();

  for (i = 0; i < lines.len; i++) {
    if (!stralloc_append(&line,i + lines.s)) die_nomem();
    if (lines.s[i] == '\0' || i == lines.len - 1) {
      backwards_rules = load_filter_rule(backwards_rules,&line);
      blank(&line);
    }
  }

  return reverse_backwards_rules_to_match_file_order(backwards_rules);
}

int read_and_process_until_either_end_closes(int from_client,int to_server,
                                             int from_server,int to_client,
                                             stralloc *greeting,
                                             filter_rule *rules) {
  char buf[PIPE_READ_SIZE];
  int exitcode = USE_CHILD_EXITCODE_LATER;
  int want_data = 0, in_data = 0;
  stralloc partial_request = {0}, partial_response = {0};
  stralloc client_eof = {0};
  request_response rr;

  copys(&client_eof,PSEUDOVERB_CLIENTEOF);
  request_response_init(&rr);

  copys(rr.client_verb,PSEUDOVERB_GREETING);

  for (;;) {
    want_to_read(from_client,from_server);
    if (!can_read_something(from_client,from_server)) continue;

    if (can_read(from_client)) {
      if (!safeappend(&partial_request,from_client,buf,sizeof buf)) {
        munge_response_line(&partial_request,0,&exitcode,greeting,rules,&client_eof);
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

    if (request_needs_handling(&rr))
      handle_client_request(to_server,rules,&rr,&want_data,&in_data);

    if (response_needs_handling(&rr))
      exitcode = handle_server_response(to_client,greeting,rules,&rr,&want_data,&in_data);

    if (exitcode != USE_CHILD_EXITCODE_LATER) break;
  }

  return exitcode;
}

void teardown_and_exit(int exitcode,int child,int from_server,int to_server) {
  int wstat;

  close(from_server);
  close(to_server);

  if (wait_pid(&wstat,child) == -1) die_wait();
  if (wait_crashed(wstat)) die_crash();

  if (exitcode == USE_CHILD_EXITCODE_LATER) _exit(wait_exitcode(wstat));
  else _exit(exitcode);
}

void be_parent(int from_client,int to_client,
               int from_me,int to_me,
               int from_server,int to_server,
               stralloc *greeting,filter_rule *rules,
               int child) {
  int exitcode;

  setup_parent(from_me,to_me);
  exitcode = read_and_process_until_either_end_closes(from_client,to_server,
                                                      from_server,to_client,
                                                      greeting,rules);
  teardown_and_exit(exitcode,child,from_server,to_server);
}

void load_smtp_greeting(stralloc *greeting,char *configfile) {
  if (control_init() == -1) die_control();
  if (control_rldef(greeting,configfile,1,(char *) 0) != 1) die_control();
}

void cd_var_qmail() {
  if (chdir(auto_qmail) == -1) die_control();
}

int main(int argc,char **argv) {
  stralloc greeting = {0};
  filter_rule *rules;
  int from_client;
  int from_me, to_server;
  int from_server, to_me;
  int to_client;
  int child;

  argv++; if (!*argv) die_usage();

  cd_var_qmail();
  load_smtp_greeting(&greeting,"control/smtpgreeting");
  rules = load_filter_rules("control/fixsmtpio");

  from_client = 0;
  mypipe(&from_me,&to_server);
  mypipe(&from_server,&to_me);
  to_client = 1;

  if ((child = fork()))
    be_parent(from_client,to_client,
              from_me,to_me,
              from_server,to_server,
              &greeting,rules,
              child);
  else if (child == 0)
    be_child(from_me,to_me,
             from_server,to_server,
             argv);
  else
    die_fork();
}

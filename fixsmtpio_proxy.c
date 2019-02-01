#include <libgen.h>
#include <signal.h>

#include "fmt.h"

#include "fixsmtpio_proxy.h"
#include "fixsmtpio_readwrite.h"
#include "fixsmtpio_die.h"
#include "fixsmtpio_eventq.h"
#include "fixsmtpio_filter.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_ucspitls.h"
#include "acceptutils_unistd.h"

int ends_data(stralloc *r) {
  int len = r->len;

  if (!len                      ) return 0;
  if (       r->s[--len] != '\n') return 0;
  if (len && r->s[--len] != '\r') ++len;
  if (!len                      ) return 0;
  if (       r->s[--len] !=  '.') return 0;
  if (len && r->s[--len] != '\n') return 0;

  return 1;
}

static int find_first_space(stralloc *request) {
  int i;
  for (i = 0; i < request->len; i++) if (request->s[i] == ' ') return i;
  return -1;
}

void strip_last_eol(stralloc *sa) {
  if (sa->len > 0 && sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->len > 0 && sa->s[sa->len-1] == '\r') sa->len--;
}

static void all_verb_no_arg(stralloc *verb,stralloc *arg,stralloc *request) {
  copy(verb,request);
  strip_last_eol(verb);
  blank(arg);
}

static void verb_and_arg(stralloc *verb,stralloc *arg,int pos,stralloc *request) {
  copyb(verb,request->s,pos-1);
  copyb(arg,request->s+pos,request->len-pos);
  strip_last_eol(arg);
}

void parse_client_request(stralloc *verb,stralloc *arg,stralloc *request) {
  int pos;
  pos = find_first_space(request);
  if (pos == -1)
    all_verb_no_arg(verb,arg,request);
  else
    verb_and_arg(verb,arg,++pos,request);
}

static int need_starttls_first(int tls_level,int in_tls,char *event) {
  return tls_level >= UCSPITLS_REQUIRED
    && !in_tls
    && !event_matches(EVENT_GREETING,event)
    && !event_matches(EVENT_TIMEOUT,event)
    && !event_matches(EVENT_CLIENTEOF,event)
    && !event_matches("noop",event)
    && !event_matches("ehlo",event)
    && !event_matches("starttls",event)
    && !event_matches("quit",event);
}

void construct_proxy_request(stralloc *proxy_request,
                             filter_rule *rules,
                             char *event,stralloc *arg,
                             stralloc *client_request,
                             int tls_level,
                             int *want_tls,int in_tls,
                             int *want_data) {
  filter_rule *rule;

  for (rule = rules; rule; rule = rule->next)
    if (rule->request_prepend && filter_rule_applies(rule,event))
      prepends(proxy_request,rule->request_prepend);
  if (need_starttls_first(tls_level,in_tls,event))
    prepends(proxy_request,REQUEST_NOOP PROGNAME " STARTTLS FIRST ");
  else if (event_matches("starttls",event)) {
    *want_tls = 1;
    if (tls_level >= UCSPITLS_AVAILABLE) {
      if (in_tls)
        prepends(proxy_request,REQUEST_NOOP PROGNAME " STARTTLS AGAIN ");
      else
        prepends(proxy_request,REQUEST_NOOP PROGNAME " STARTTLS BEGIN ");
    } else {
      prepends(proxy_request,REQUEST_NOOP PROGNAME " STARTTLS BLOCK ");
    }
  }
  else if (event_matches("data",event))
    *want_data = 1;
  cat(proxy_request,client_request);
}

static int accepted_data(stralloc *response) { return starts(response,"354 "); }

void construct_proxy_response(stralloc *proxy_response,
                              stralloc *greeting,
                              filter_rule *rules,
                              char *event,
                              stralloc *server_response,
                              int *proxy_exitcode,
                              int tls_level,
                              int want_tls,int in_tls,
                              int *want_data,int *in_data) {
  if (event_matches("data",event) && *want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) {
      eventq_put("in_data");
      *in_data = 1;
    }
  }

  if (event_matches("starttls",event) && want_tls) {
    if (tls_level < UCSPITLS_AVAILABLE || in_tls)
      copys(proxy_response,"502 unimplemented (#5.5.1)\r\n");
    else
      copys(proxy_response,"220 Ready to start TLS (#5.7.0)\r\n");
    return;
  }
  if (need_starttls_first(tls_level,in_tls,event))
    copys(proxy_response,"530 Must start TLS first (#5.7.0)\r\n");
  else
    copy(proxy_response,server_response);
    munge_response(proxy_response,proxy_exitcode,greeting,rules,event,tls_level,in_tls);
}

int get_one(const char *caller,stralloc *one,stralloc *pile,int (*fn)(stralloc *)) {
  stralloc caller_sa = {0};
  int got_one = 0;
  stralloc next_pile = {0};
  int pos = 0;
  int i;

  for (i = pos; i < pile->len; i++) {
    if (pile->s[i] == '\n') {
      stralloc line = {0};

      catb(&line,pile->s+pos,i+1-pos);
      pos = i+1;
      cat(one,&line);

      if (!fn || fn(&line)) {
        got_one = 1;
        break;
      }
    }
  }

  if (got_one) {
    copys(&caller_sa,(char *)caller);
    cats(&caller_sa,":");
    cats(&caller_sa,(char *)__func__);
    append0(&caller_sa);

    contextlogging_copyb(caller_sa.s,&next_pile,pile->s+pos,pile->len-pos);
    contextlogging_copy(caller_sa.s,pile,&next_pile);
    blank(&next_pile);

    blank(&caller_sa);
  } else {
    blank(one);
  }

  return got_one;
}

int get_one_request(stralloc *one,stralloc *pile) {
  return get_one(__func__,one,pile,0);
}

int is_last_line_of_response(stralloc *line) {
  return line->len >= 4 && line->s[3] == ' ';
}

int get_one_response(stralloc *one,stralloc *pile) {
  return get_one(__func__,one,pile,&is_last_line_of_response);
}

static void handle_data_specially(stralloc *data,int *in_data,stralloc *logstamp) {
  logit(logstamp,'D',data);
  if (ends_data(data))
    *in_data = 0;
}

static void handle_request(stralloc *proxy_request,stralloc *request,int tls_level,int *want_tls,int in_tls,int *want_data,filter_rule *rules,stralloc *logstamp) {
  stralloc event = {0}, verb = {0}, arg = {0};

  logit(logstamp,'1',request);
  parse_client_request(&verb,&arg,request);
  copy(&event,&verb);
  append0(&event);
  eventq_put(event.s);
  construct_proxy_request(proxy_request,rules,
                          event.s,&arg,
                          request,
                          tls_level,
                          want_tls,in_tls,
                          want_data);
  blank(request);
  logit(logstamp,'2',proxy_request);
}

static void handle_response(stralloc *proxy_response,int *exitcode,stralloc *response,int tls_level,int want_tls,int in_tls,int *want_data,int *in_data,filter_rule *rules,stralloc *greeting,stralloc *logstamp) {
  char *event;
  logit(logstamp,'3',response);
  event = eventq_get();
  construct_proxy_response(proxy_response,
                           greeting,rules,event,
                           response,
                           exitcode,
                           tls_level,
                           want_tls,in_tls,
                           want_data,in_data);
  logit(logstamp,'4',proxy_response);
  alloc_free(event);
  blank(response);
}

static void use_as_stdin(int fd)  { if (fd_move(0,fd) == -1) die_pipe(); }
static void use_as_stdout(int fd) { if (fd_move(1,fd) == -1) die_pipe(); }

static void make_pipe(int *from,int *to) {
  int pi[2];
  if (unistd_pipe(pi) == -1) die_pipe();
  *from = pi[0];
  *to = pi[1];
}

static void be_proxied(int from_proxy,int to_proxy,
                       int from_proxied,int to_proxied,
                       char **argv) {
  unistd_close(from_proxied);
  unistd_close(to_proxied);
  use_as_stdin(from_proxy);
  use_as_stdout(to_proxy);
  unistd_execvp(*argv,argv);
  die_exec();
}

static char *format_pid(unsigned int pid) {
  char pidbuf[FMT_ULONG];
  stralloc sa = {0};
  if (!sa.len) {
    int len = fmt_ulong(pidbuf,pid);
    if (len) copyb(&sa,pidbuf,len);
    append0(&sa);
  }
  return sa.s;
}

static void prepare_logstamp(stralloc *sa,int kid_pid,char *kid_name) {
  copys(sa,PROGNAME " ");
  cats(sa,format_pid(unistd_getpid())); cats(sa," ");
  cats(sa,kid_name);                    cats(sa," ");
  cats(sa,format_pid(kid_pid));         cats(sa," ");
}

static void stop_kid_and_maybe_myself(int exitcode,int kid_pid,
                                      int from_server,int to_server) {
  int wstat;
  int startingtls = (exitcode == BEGIN_STARTTLS_NOW);

  unistd_close(from_server);
  unistd_close(to_server);

  if (startingtls && -1 == kill(kid_pid,SIGTERM)) die_kill();

  if (wait_pid(&wstat,kid_pid) == -1) die_wait();

  if (startingtls) return;

  if (wait_crashed(wstat)) die_crash();

  if (exitcode == EXIT_LATER_NORMALLY)
    unistd_exit(wait_exitcode(wstat));
  else
    unistd_exit(exitcode);
}

static void run_new_kid_in_read_loop(int *from_client,int *to_proxy,
                                    int *from_proxy,int *to_server,
                                    int *from_server,int *to_client,
                                    stralloc *logstamp,stralloc *greeting,
                                    filter_rule *rules,char **argv,
                                    int in_tls) {
  int kid_pid;

  make_pipe(from_proxy,to_server);
  make_pipe(from_server,to_proxy);
  kid_pid = unistd_fork();

  if (kid_pid) {
    unistd_close(*from_proxy);
    unistd_close(*to_proxy);
    prepare_logstamp(logstamp,kid_pid,basename(argv[0]));
    eventq_put(EVENT_GREETING);
    stop_kid_and_maybe_myself(
        read_and_process_until_either_end_closes(*from_client,*to_server,
                                                 *from_server,*to_client,
                                                 greeting,rules,
                                                 logstamp,in_tls),
        kid_pid,*from_server,*to_server);
  } else if (0 == kid_pid) {
    be_proxied(*from_proxy,*to_proxy,
               *from_server,*to_server,
               argv);
  } else {
    die_fork();
  }
}

void be_proxy(stralloc *greeting,filter_rule *rules,char **argv) {
  int from_client = 0, to_proxy;
  int from_proxy, to_server;
  int from_server, to_client = 1;
  stralloc logstamp = {0};

  for (int in_tls = 0; in_tls <= 1; in_tls++)
    run_new_kid_in_read_loop(&from_client,&to_proxy,
                             &from_proxy,&to_server,
                             &from_server,&to_client,
                             &logstamp,greeting,
                             rules,argv,
                             in_tls);
}

int read_and_process_until_either_end_closes(int from_client,int to_server,
                                             int from_server,int to_client,
                                             stralloc *greeting,
                                             filter_rule *rules,
                                             stralloc *logstamp,
                                             int in_tls) {
  char     buf               [SUBSTDIO_INSIZE];
  int      exitcode         = EXIT_LATER_NORMALLY;
  int      tls_level        = ucspitls_level(),
           want_tls         =  0,
           want_data        =  0, in_data = 0;
  stralloc client_requests  = {0}, one_request  = {0}, proxy_request  = {0},
           server_responses = {0}, one_response = {0}, proxy_response = {0};

  for (;;) {
    if (!block_efficiently_until_can_read_either(from_client,from_server)) break;

    if (can_read(from_client)) {
      if (!safeappend(&client_requests,from_client,buf,sizeof buf)) {
        munge_response_line(0,&client_requests,&exitcode,greeting,rules,EVENT_CLIENTEOF,tls_level,in_tls);
        break;
      }
      while (client_requests.len) {
        if (in_data) {
          handle_data_specially(&client_requests,&in_data,logstamp);
          safewrite(to_server,&client_requests);
        } else if (get_one_request(&one_request,&client_requests)) {
          handle_request(&proxy_request,&one_request,tls_level,&want_tls,in_tls,&want_data,rules,logstamp);
          safewrite(to_server,&proxy_request);
        }
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&server_responses,from_server,buf,sizeof buf)) break;
      while (server_responses.len && exitcode == EXIT_LATER_NORMALLY && get_one_response(&one_response,&server_responses)) {
        handle_response(&proxy_response,&exitcode,&one_response,tls_level,want_tls,in_tls,&want_data,&in_data,rules,greeting,logstamp);
        safewrite(to_client,&proxy_response);
        if (want_tls) {
          want_tls = 0;
          if (tls_level >= UCSPITLS_AVAILABLE && !in_tls) {
            if (!tls_init() || !tls_info(die_nomem)) die_tls();
            if (!env_put("FIXSMTPIOTLS=1")) die_nomem(__func__,"env_put");
            exitcode = BEGIN_STARTTLS_NOW;
          }
        }
      }
    }

    if (exitcode != EXIT_LATER_NORMALLY) break;
  }

  return exitcode;
}

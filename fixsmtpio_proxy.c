#include "fixsmtpio_proxy.h"
#include "fixsmtpio_readwrite.h"
#include "fixsmtpio_die.h"
#include "fixsmtpio_eventq.h"
#include "fixsmtpio_filter.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_ucspitls.h"

int is_last_line_of_data(stralloc *r) {
  return (r->len == 3 && r->s[0] == '.' && r->s[1] == '\r' && r->s[2] == '\n');
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
                             int *want_data,int *in_data) {
  filter_rule *rule;

  if (*in_data) {
    copy(proxy_request,client_request);
    if (is_last_line_of_data(proxy_request)) *in_data = 0;
  } else {
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
  if (*want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) *in_data = 1;
  }

  if (want_tls) {
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
    copys(&next_pile,"");

    copys(&caller_sa,"");
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

void handle_request(stralloc *proxy_request,stralloc *request,int tls_level,int *want_tls,int in_tls,int *want_data,int *in_data,filter_rule *rules,int kid_pid,char *kid_name) {
  stralloc event = {0}, verb = {0}, arg = {0};

  logit('1',kid_pid,kid_name,request);
  if (!*in_data) parse_client_request(&verb,&arg,request);
  copy(&event,&verb);
  append0(&event);
  eventq_put(event.s);
  construct_proxy_request(proxy_request,rules,
                          event.s,&arg,
                          request,
                          tls_level,
                          want_tls,in_tls,
                          want_data,in_data);
  logit('2',kid_pid,kid_name,proxy_request);
  blank(request);
}

void handle_response(stralloc *proxy_response,int *exitcode,stralloc *response,int tls_level,int want_tls,int in_tls,int *want_data,int *in_data,filter_rule *rules,stralloc *greeting,int kid_pid,char *kid_name) {
  char *event;
  logit('3',kid_pid,kid_name,response);
  event = eventq_get();
  construct_proxy_response(proxy_response,
                           greeting,rules,event,
                           response,
                           exitcode,
                           tls_level,
                           want_tls,in_tls,
                           want_data,in_data);
  logit('4',kid_pid,kid_name,proxy_response);
  alloc_free(event);
  blank(response);
}

int read_and_process_until_either_end_closes(int from_client,int to_server,
                                             int from_server,int to_client,
                                             stralloc *greeting,
                                             filter_rule *rules,
                                             int kid_pid,char *kid_name) {
  char buf[SUBSTDIO_INSIZE];

  int      exitcode         = EXIT_LATER_NORMALLY;

  int      tls_level        = ucspitls_level(),
           want_tls         =  0,
           in_tls           =  0,
           want_data        =  0,
           in_data          =  0;

  stralloc client_requests  = {0},
           one_request      = {0},
           proxy_request    = {0},
           server_responses = {0},
           one_response     = {0},
           proxy_response   = {0};

  eventq_put(EVENT_GREETING);

  for (;;) {
    if (!block_efficiently_until_can_read_either(from_client,from_server)) break;

    if (can_read(from_client)) {
      if (!safeappend(&client_requests,from_client,buf,sizeof buf)) {
        munge_response_line(0,&client_requests,&exitcode,greeting,rules,EVENT_CLIENTEOF,tls_level,in_tls);
        break;
      }
      while (client_requests.len && get_one_request(&one_request,&client_requests)) {
        handle_request(&proxy_request,&one_request,tls_level,&want_tls,in_tls,&want_data,&in_data,rules,kid_pid,kid_name);
        safewrite(to_server,&proxy_request);
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&server_responses,from_server,buf,sizeof buf)) break;
      while (server_responses.len && exitcode == EXIT_LATER_NORMALLY && get_one_response(&one_response,&server_responses)) {
        handle_response(&proxy_response,&exitcode,&one_response,tls_level,want_tls,in_tls,&want_data,&in_data,rules,greeting,kid_pid,kid_name);
        safewrite(to_client,&proxy_response);
        if (want_tls) {
          want_tls = 0;
          if (tls_level >= UCSPITLS_AVAILABLE && !in_tls) {
            if (!tls_init()) die_tls();
            in_tls = 1;
          }
        }
      }
    }

    if (exitcode != EXIT_LATER_NORMALLY) break;
  }

  return exitcode;
}

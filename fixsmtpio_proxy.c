#include "fixsmtpio_proxy.h"
#include "fixsmtpio_readwrite.h"
#include "fixsmtpio_common.h"
#include "fixsmtpio_eventq.h"
#include "fixsmtpio_filter.h"

/*
  NULL: false
  empty: false
  " \r\n": false
  ".\r\n": true
 */
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

/*
  in_data: whatever client_request is, proxy_request is exactly the same
  not in_data, a rule says to prepend: proxy request is equal to prepended + client_request
  not in_data, two rules prepend: proxy request is equal to p1 + p2 + client_request (WILL FAIL)

  not sure whether to test in_data/want_data state changes
 */
void construct_proxy_request(stralloc *proxy_request,
                             filter_rule *rules,
                             char *event,stralloc *arg,
                             stralloc *client_request,
                             int *want_data,int *in_data) {
  filter_rule *rule;

  if (*in_data) {
    copy(proxy_request,client_request);
    if (is_last_line_of_data(proxy_request)) *in_data = 0;
  } else {
    for (rule = rules; rule; rule = rule->next)
      if (rule->request_prepend && filter_rule_applies(rule,event))
        prepends(proxy_request,rule->request_prepend);
    if (event_matches("data",event)) *want_data = 1;
    cat(proxy_request,client_request);
  }
}

static int accepted_data(stralloc *response) { return starts(response,"354 "); }

/*
  not sure whether to test:
  not want_data, not in_data, no request_received, no verb: it's a timeout
 */
void construct_proxy_response(stralloc *proxy_response,
                              stralloc *greeting,
                              filter_rule *rules,
                              char *event,
                              stralloc *server_response,
                              int *proxy_exitcode,
                              int *want_data,int *in_data) {
  if (*want_data) {
    *want_data = 0;
    if (accepted_data(server_response)) *in_data = 1;
  }
  copy(proxy_response,server_response);
  munge_response(proxy_response,proxy_exitcode,greeting,rules,event);
}

void logit(char logprefix,stralloc *sa) {
  if (!env_get("FIXSMTPIODEBUG")) return;
  substdio_put(&sserr,&logprefix,1);
  substdio_puts(&sserr,": ");
  substdio_put(&sserr,sa->s,sa->len);
  if (!ends_with_newline(sa)) substdio_puts(&sserr,"\r\n");
  substdio_flush(&sserr);
}

int get_one(stralloc *one,stralloc *pile,int (*fn)(stralloc *)) {
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
    copyb(&next_pile,pile->s+pos,pile->len-pos);
    copy(pile,&next_pile);
  } else {
    blank(one);
  }

  return got_one;
}

int get_one_request(stralloc *one,stralloc *pile) {
  return get_one(one,pile,0);
}

int is_last_line_of_response(stralloc *line) {
  return line->len >= 4 && line->s[3] == ' ';
}

int get_one_response(stralloc *one,stralloc *pile) {
  return get_one(one,pile,&is_last_line_of_response);
}

void handle_request(stralloc *proxy_request,stralloc *request,int *want_data,int *in_data,filter_rule *rules) {
  stralloc event = {0}, verb = {0}, arg = {0};

  logit('1',request);
  if (!*in_data) parse_client_request(&verb,&arg,request);
  logit('2',&verb);
  logit('3',&arg);
  copy(&event,&verb);
  if (!stralloc_0(&event)) die_nomem();
  eventq_put(event.s);
  construct_proxy_request(proxy_request,rules,
                          event.s,&arg,
                          request,
                          want_data,in_data);
  logit('4',proxy_request);
  blank(request);
}

void handle_response(stralloc *proxy_response,int *exitcode,stralloc *response,int *want_data,int *in_data,filter_rule *rules,stralloc *greeting) {
  char *event;
  logit('5',response);
  event = eventq_get();
  construct_proxy_response(proxy_response,
                           greeting,rules,event,
                           response,
                           exitcode,
                           want_data,in_data);
  logit('6',proxy_response);
  //alloc_free(event);
  blank(response);
}

int read_and_process_until_either_end_closes(int from_client,int to_server,
                                             int from_server,int to_client,
                                             stralloc *greeting,
                                             filter_rule *rules) {
  char buf[SUBSTDIO_INSIZE];

  int      exitcode         = EXIT_LATER_NORMALLY;

  int      want_data        =  0,
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
        munge_response_line(0,&client_requests,&exitcode,greeting,rules,EVENT_CLIENTEOF);
        break;
      }
      while (client_requests.len && get_one_request(&one_request,&client_requests)) {
        handle_request(&proxy_request,&one_request,&want_data,&in_data,rules);
        safewrite(to_server,&proxy_request);
      }
    }

    if (can_read(from_server)) {
      if (!safeappend(&server_responses,from_server,buf,sizeof buf)) break;
      while (server_responses.len && exitcode == EXIT_LATER_NORMALLY && get_one_response(&one_response,&server_responses)) {
        handle_response(&proxy_response,&exitcode,&one_response,&want_data,&in_data,rules,greeting);
        safewrite(to_client,&proxy_response);
      }
    }

    if (exitcode != EXIT_LATER_NORMALLY) break;
  }

  return exitcode;
}

#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"
#include "fixsmtpio_common.h"
#include "fixsmtpio_munge.h"
#include "fixsmtpio_glob.h"

int want_munge_internally(char *response) {
  return 0 == str_diffn(MUNGE_INTERNALLY,response,sizeof(MUNGE_INTERNALLY)-1);
}

int want_munge_from_config(char *response) {
  return 0 != str_diffn(RESPONSELINE_NOCHANGE,response,sizeof(RESPONSELINE_NOCHANGE)-1);
}

int envvar_exists_if_needed(char *envvar) {
  if (envvar) {
    if (!str_diff("",envvar)) return 1;
    if (env_get(envvar)) return 1;
    return 0;
  }
  return 1;
}

// XXX don't test this directly
int filter_rule_applies(filter_rule *rule,const char *event) {
  return (event_matches(rule->event,event) && envvar_exists_if_needed(rule->env));
}

void munge_response_line(int lineno,
                         stralloc *line,int *exitcode,
                         stralloc *greeting,filter_rule *rules,const char *event) {
  filter_rule *rule;
  stralloc line0 = {0};

  copy(&line0,line);
  if (!stralloc_0(&line0)) die_nomem();

  for (rule = rules; rule; rule = rule->next) {
    if (!filter_rule_applies(rule,event)) continue;
    if (!string_matches_glob(rule->response_line_glob,line0.s)) continue;
    munge_exitcode(exitcode,rule);
    if (want_munge_internally(rule->response))
      munge_line_internally(line,lineno,greeting,event);
    else if (want_munge_from_config(rule->response))
      copys(line,rule->response);
  }
  if (line->len) if (!ends_with_newline(line)) cats(line,"\r\n");
}

void munge_response(stralloc *response,int *exitcode,
                    stralloc *greeting,filter_rule *rules,const char *event) {
  stralloc munged = {0};
  stralloc line = {0};
  int lineno = 0;
  int i;

  for (i = 0; i < response->len; i++) {
    if (!stralloc_append(&line,i + response->s)) die_nomem();
    if (response->s[i] == '\n' || i == response->len - 1) {
      munge_response_line(lineno++,&line,exitcode,greeting,rules,event);
      cat(&munged,&line);
      blank(&line);
    }
  }

  if (munged.len) reformat_multiline_response(&munged);
  copy(response,&munged);
}

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

filter_rule *reverse_rules(filter_rule *rules) {
  filter_rule *reversed_rules = 0;
  filter_rule *temp;

  while (rules) {
    temp = rules;
    rules = rules->next;
    temp->next = reversed_rules;
    reversed_rules = temp;
  }

  return reversed_rules;
}

filter_rule *load_filter_rules(void) {
  filter_rule *backwards_rules = 0;

  // if client closes the connection, tell authup to be happy
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          EVENT_CLIENTEOF,
      REQUEST_PASSTHRU,         "*",
      EXIT_NOW_SUCCESS,         ""
  );

  // if server greets us unhappily, notify authup
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          EVENT_GREETING,
      REQUEST_PASSTHRU,         "4*",
      EXIT_NOW_TEMPFAIL,        0
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          EVENT_GREETING,
      REQUEST_PASSTHRU,         "5*",
      EXIT_NOW_PERMFAIL,        0
  ); // XXX LEAVE_RESPONSE_LINE_AS_IS

  // if server times out, hide message (authup has its own)
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          EVENT_TIMEOUT,
      REQUEST_PASSTHRU,         "*",
      EXIT_NOW_TIMEOUT,         ""
  ); // XXX REMOVE_RESPONSE_LINE

  // always replace hostname in greeting
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  EVENT_GREETING,
      REQUEST_PASSTHRU,         "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // if authenticated, replace greeting entirely
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          EVENT_GREETING,
      REQUEST_PASSTHRU,         "2*",
      EXIT_LATER_NORMALLY,      "235 ok, go ahead (#2.0.0)"
  );

  // implement a new verb that is not very interesting
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "word",
      REQUEST_NOOP,             "*",
      EXIT_LATER_NORMALLY,      "250 likewise, give my regards to your mother"
  );

  // always replace greeting in HELO/EHLO
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "helo",
      REQUEST_PASSTHRU,         "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "ehlo",
      REQUEST_PASSTHRU,         "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // always prepend acceptutils link to HELP message
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "help",
      REQUEST_PASSTHRU,         "*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // always replace greeting in QUIT
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "quit",
      REQUEST_PASSTHRU,         "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // don't advertise AUTH or STARTTLS
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          "ehlo",
      REQUEST_PASSTHRU,         "250?AUTH*",
      EXIT_LATER_NORMALLY,      ""
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          "ehlo",
      REQUEST_PASSTHRU,         "250?STARTTLS",
      EXIT_LATER_NORMALLY,      ""
  );

  // don't allow AUTH or STARTTLS
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          "auth",
      REQUEST_NOOP,             "*",
      EXIT_LATER_NORMALLY,      "502 unimplemented (#5.5.1)"
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUP_USER,          "starttls",
      REQUEST_NOOP,             "*",
      EXIT_LATER_NORMALLY,      "502 unimplemented (#5.5.1)"
  );

  return reverse_rules(backwards_rules);
}

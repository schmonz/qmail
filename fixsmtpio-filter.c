#include "fixsmtpio.h"
#include "fixsmtpio-filter.h"

void strip_last_eol(stralloc *sa) {
  if (sa->len > 0 && sa->s[sa->len-1] == '\n') sa->len--;
  if (sa->len > 0 && sa->s[sa->len-1] == '\r') sa->len--;
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
}

int string_matches_glob(char *glob,char *string) {
  return 0 == fnmatch(glob,string,0);
}

int want_munge_internally(char *response) {
  return 0 == str_diffn(MUNGE_INTERNALLY,response,sizeof(MUNGE_INTERNALLY)-1);
}

int want_munge_from_config(char *response) {
  return 0 != str_diffn(RESPONSELINE_NOCHANGE,response,sizeof(RESPONSELINE_NOCHANGE)-1);
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
  if (rule->exitcode != EXIT_LATER_NORMALLY) *exitcode = rule->exitcode;
}

void munge_response_line(int lineno,
                         stralloc *line,int *exitcode,
                         stralloc *greeting,filter_rule *rules,
                         stralloc *verb) {
  filter_rule *rule;
  stralloc line0 = {0};

  copy(&line0,line);
  if (!stralloc_0(&line0)) die_nomem();

  for (rule = rules; rule; rule = rule->next) {
    if (!filter_rule_applies(rule,verb)) continue;
    if (!string_matches_glob(rule->response_line_glob,line0.s)) continue;
    munge_exitcode(exitcode,rule);
    if (want_munge_internally(rule->response))
      munge_line_internally(line,lineno,greeting,verb);
    else if (want_munge_from_config(rule->response))
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
      munge_response_line(lineno++,&line,exitcode,greeting,rules,verb);
      cat(&munged,&line);
      blank(&line);
    }
  }

  reformat_multiline_response(&munged);
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

void free_if_non_null(char *env,char *event,char *request_prepend,
                      char *response_line_glob,char *response) {
  return; // XXX
  if (env) alloc_free(env);
  if (event) alloc_free(event);
  if (request_prepend) alloc_free(request_prepend);
  if (response_line_glob) alloc_free(response_line_glob);
  if (response) alloc_free(response);
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

void unload_filter_rules(filter_rule *rules) {
  filter_rule *temp;

  while (rules) {
    temp = rules;
    rules = rules->next;
    free_if_non_null(temp->env,temp->event,temp->request_prepend,
                     temp->response_line_glob,temp->response);
    alloc_free(temp);
  }
}

filter_rule *load_filter_rules(void) {
  filter_rule *backwards_rules = 0;

  // if client closes the connection, tell qmail-authup to be happy
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             PSEUDOVERB_CLIENTEOF,
      PREPEND_NOTHING,          "*",
      EXIT_NOW_SUCCESS,         ""
  );

  // if server greets us unhappily, notify qmail-authup
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             PSEUDOVERB_GREETING,
      PREPEND_NOTHING,          "4*",
      EXIT_NOW_TEMPFAIL,        0
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             PSEUDOVERB_GREETING,
      PREPEND_NOTHING,          "5*",
      EXIT_NOW_PERMFAIL,        0
  ); // XXX LEAVE_RESPONSE_LINE_AS_IS

  // if server times out, hide message (qmail-authup has its own)
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             PSEUDOVERB_TIMEOUT,
      PREPEND_NOTHING,          "*",
      EXIT_NOW_TIMEOUT,         ""
  ); // XXX REMOVE_RESPONSE_LINE

  // if authenticated, replace greeting
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             PSEUDOVERB_GREETING,
      PREPEND_NOTHING,          "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // implement a new verb that is not very interesting
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "word",
      PREPEND_VERB_NOOP,        "*",
      EXIT_LATER_NORMALLY,      "250 likewise, give my regards to your mother"
  );

  // always replace greeting in HELO/EHLO
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "helo",
      PREPEND_NOTHING,          "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "ehlo",
      PREPEND_NOTHING,          "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // always prepend acceptutils link to HELP message
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "help",
      PREPEND_NOTHING,          "*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // always replace greeting in QUIT
  backwards_rules = prepend_rule(backwards_rules,
      ENV_ANY,                  "quit",
      PREPEND_NOTHING,          "2*",
      EXIT_LATER_NORMALLY,      MUNGE_INTERNALLY
  );

  // don't advertise AUTH or STARTTLS
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             "ehlo",
      PREPEND_NOTHING,          "250?AUTH*",
      EXIT_LATER_NORMALLY,      ""
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             "ehlo",
      PREPEND_NOTHING,          "250?STARTTLS",
      EXIT_LATER_NORMALLY,      ""
  );

  // don't allow AUTH or STARTTLS
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             "auth",
      PREPEND_VERB_NOOP,        "*",
      EXIT_LATER_NORMALLY,      "502 unimplemented (#5.5.1)"
  );
  backwards_rules = prepend_rule(backwards_rules,
      ENV_AUTHUSER,             "starttls",
      PREPEND_VERB_NOOP,        "*",
      EXIT_LATER_NORMALLY,      "502 unimplemented (#5.5.1)"
  );

  return reverse_rules(backwards_rules);
}


#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"
#include "fixsmtpio_common.h"

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

/*
 * s is NULL
 * s is not 0-terminated
 * s is empty
 * sa is NULL
 * sa->len is 0
 * strings differ, don't match
 * strings same, do match
 * strings same except case, do match
 */
int event_matches(char *s,const char *s2) {
  if (!str_len(s2)) return 0;
  return !case_diffs(s,s2);
}

/*
 * input is null
 * input is empty
 * input is less than four chars long
 * input is exactly four chars long
 * input is longer than four, but still one line
 * input is multiple lines, first one long enough, second one not
 * input is multiple lines, first one short, second one long enough
 * input is multiple lines, first one long enough, second one not, third one long enough
 */
void change_every_line_fourth_char_to_dash(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = 0; i < multiline->len; i++) {
    if (multiline->s[i] == '\n') pos = -1;
    if (pos == 3) multiline->s[i] = '-';
    pos++;
  }
}

/*
 * input is null
 * input is empty
 * input is less than four chars long
 * input is exactly four chars long
 * input is longer than four, but still one line
 * input is multiple lines, first one long enough, second one not
 * input is multiple lines, first one short, second one long enough
 * input is multiple lines, first one long enough, second one not, third one long enough
 */
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
  { EVENT_GREETING, munge_greeting }
, { "ehlo", munge_ehlo }
, { "helo", munge_helo }
, { "help", munge_help }
, { "quit", munge_quit }
, { 0, 0 }
};

void *munge_line_fn(const char *event) {
  int i;
  for (i = 0; m[i].event; ++i)
    if (event_matches(m[i].event,event))
      return m[i].munger;
  return 0;
}

/*
 not requests, response lines!

 ("250 word up, kids", 0, "yo.sup.local", "word") -> "250 word up, kids"

 ("250-applesauce",     0, "yo.sup.local", "ehlo") -> "250-yo.sup.local"
 ("250-STARTSOMETHING", 1, "yo.sup.local", "ehlo") -> "250-STARTSOMETHING"
 ("250 ENDSOMETHING",   2, "yo.sup.local", "ehlo") -> "250 ENDSOMETHING"

 ("250 applesauce",     0, "yo.sup.local", "helo") -> "250-yo.sup.local"

 ("214 ask your grandmother", 0, "yo.sup.local", "help") -> "214 https://....\r\n214 ask your grandmother\r\n"

 ("221 get outta here", 0, "yo.sup.local", "quit") -> "221 yo.sup.local"
 */
void munge_line_internally(stralloc *line,int lineno,
                           stralloc *greeting,const char *event) {
  void (*munger)() = munge_line_fn(event);
  if (munger) munger(line,lineno,greeting);
}

/*
 don't test fnmatch(), document what control/fixsmtpio needs from it

 glob is "*"
 glob is "4*"
 glob is "5*"
 glob is "2*"
 glob is "250?AUTH*"
 glob is "250?auth*"
 glob is "250?STARTTLS"

 string is empty
 string is "450 tempfail"
 string is "I have eaten 450 french fries"
 string is "250-auth login"
 string is "the anthology contains works by 250 authors"

 maybe this should be regex instead of glob?
 */
int string_matches_glob(char *glob,char *string) {
  return 0 == fnmatch(glob,string,0);
}

/*
 "&fixsmtpio" -> yes
 "&nofixsmtpio" -> no
 "" -> no
 NULL -> no
 "whatever else" -> no
 */
int want_munge_internally(char *response) {
  return 0 == str_diffn(MUNGE_INTERNALLY,response,sizeof(MUNGE_INTERNALLY)-1);
}

/*
 "&nofixsmtpio" -> no
 "&fixsmtpio" -> yes
 "" -> yes
 NULL -> yes
 "whatever else" -> yes
 */
int want_munge_from_config(char *response) {
  return 0 != str_diffn(RESPONSELINE_NOCHANGE,response,sizeof(RESPONSELINE_NOCHANGE)-1);
}

/*
 "VERY_UNLIKELY_TO_BE_SET": no
 "": yes
 NULL: yes
 */
int envvar_exists_if_needed(char *envvar) {
  if (envvar && env_get(envvar)) return 1;
  else if (envvar) return 0;
  return 1;
}

// XXX don't test this directly
int filter_rule_applies(filter_rule *rule,const char *event) {
  return (event_matches(rule->event,event) && envvar_exists_if_needed(rule->env));
}

void munge_exitcode(int *exitcode,filter_rule *rule) {
  if (rule->exitcode != EXIT_LATER_NORMALLY) *exitcode = rule->exitcode;
}

/*
 already tested:
 - filter_rule_applies()
 - string_matches_glob()
 - want_munge_internally()
 - munge_line_internally()
 - want_munge_from_config()

 so just a couple integrated examples. with no rules:
 (0, "222 sup duuuude", 0, "yo.sup.local", (filter_rule *)0, "ehlo") -> "222 sup duuuude"
 (1, "222 OUTSTANDING", 0, "yo.sup.local", (filter_rule *)0, "ehlo") -> "222 OUTSTANDING"

 with a couple filter rules that don't apply:
 (0, "222 sup duuuude", 0, "yo.sup.local", rules, "ehlo") -> "222 sup duuuude"
 (1, "222 OUTSTANDING", 0, "yo.sup.local", rules, "ehlo") -> "222 OUTSTANDING"

 with a couple more filter rules and some do apply:
 (0, "222 sup duuuude", 0, "yo.sup.local", rules, "ehlo") -> "222 yo.sup.local"
 (1, "222 OUTSTANDING", 0, "yo.sup.local", rules, "ehlo") -> ""
 */
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

/*
 correctly splitting into lines?

 response is NULL
 response is empty
 response does not end with a newline
 response is multiline, first line ends with newline, second line does not
 */
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

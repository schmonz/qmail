#include "fixsmtpio.h"
#include "fixsmtpio_control.h"
#include "fixsmtpio_filter.h"
#include "fixsmtpio_common.h"
#include "fixsmtpio_munge.h"
#include "fixsmtpio_glob.h"

int want_munge_internally(char *response) {
  return 0 == str_diffn(MUNGE_INTERNALLY,response,sizeof(MUNGE_INTERNALLY)-1);
}

int want_leave_line_as_is(char *response) {
  return 0 == str_diffn(RESPONSELINE_NOCHANGE,response,sizeof(RESPONSELINE_NOCHANGE)-1);
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
    else if (!want_leave_line_as_is(rule->response))
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

filter_rule *prepend_rule(filter_rule *next, filter_rule *rule) {
  rule->next = next;
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

  stralloc lines = {0};
  int linestart;
  int pos;

  if (control_readfile(&lines,"control/fixsmtpio",0) == -1) die_control();

  for (linestart = 0, pos = 0; pos < lines.len; pos++) {
    if (lines.s[pos] == '\0') {
      stralloc line = {0}; stralloc_copys(&line, lines.s + linestart);
      filter_rule *rule = parse_control_line(&line);
      if (0 == rule) die_control();
      backwards_rules = prepend_rule(backwards_rules, rule);
      linestart = pos + 1;
    }
  }

  return reverse_rules(backwards_rules);
}

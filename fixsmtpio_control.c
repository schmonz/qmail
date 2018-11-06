#include "fixsmtpio_control.h"
#include "fixsmtpio_munge.h"

#include "acceptutils_stralloc.h"

static void parse_field(int *fields_seen, stralloc *value, filter_rule *rule) {
  char *s;

  (*fields_seen)++;

  if (!value->len) return;

  append0(value);
  s = (char *)alloc(value->len);
  str_copy(s, value->s);
  copys(value, "");

  switch (*fields_seen) {
    case 1: rule->env                = s; break;
    case 2: rule->event              = s; break;
    case 3: rule->request_prepend    = s; break;
    case 4: rule->response_line_glob = s; break;
    case 5:
      if (!scan_ulong(s,&rule->exitcode))
        rule->exitcode = 777;
                                          break;
    case 6: rule->response           = s; break;
  }
}

filter_rule *parse_control_line(char *line) {
  filter_rule *rule = (filter_rule *)alloc(sizeof(filter_rule));
  int line_length = str_len(line);
  stralloc value = {0};
  int fields_seen = 0;
  int i;

  rule->next                = 0;

  rule->env                 = 0;
  rule->event               = 0;
  rule->request_prepend     = 0;
  rule->response_line_glob  = 0;
  rule->exitcode            = EXIT_LATER_NORMALLY;
  rule->response            = 0;

  for (i = 0; i < line_length; i++) {
    char c = line[i];
    if (':' == c && fields_seen < 5) parse_field(&fields_seen, &value, rule);
    else append(&value, &c);
  }
  parse_field(&fields_seen, &value, rule);

  if (fields_seen < 6)            return 0;
  if (!rule->event)               return 0;
  if (!rule->response_line_glob)  return 0;
  if ( rule->exitcode > 255)      return 0;
  if ( rule->response) {
    if (!case_diffs(rule->event,"clienteof"))
                                  return 0;
    if (want_munge_internally(rule->response)
        && !munge_line_fn(rule->event))
                                  return 0;
    if (str_start(rule->response,"&fixsmtpio")
        && !want_munge_internally(rule->response)
        && !want_leave_line_as_is(rule->response))
                                  return 0;
  } else {
    rule->response = "";
  }

  return rule;
}

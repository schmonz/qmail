#include "check.h"

#include "fixsmtpio_filter.h"

void assert_parsed_line(filter_rule *rule,
                        char *env,char *event,char *request_prepend,
                        char *response_line_glob,int exitcode,char *response) {
  ck_assert_ptr_null(rule->next);

  if (env == NULL) ck_assert_ptr_null(rule->env);
  else ck_assert_str_eq(env, rule->env);

  if (event == NULL) ck_assert_ptr_null(rule->event);
  else ck_assert_str_eq(event, rule->event);

  if (request_prepend == NULL) ck_assert_ptr_null(rule->request_prepend);
  else ck_assert_str_eq(request_prepend, rule->request_prepend);

  if (response_line_glob == NULL) ck_assert_ptr_null(rule->response_line_glob);
  else ck_assert_str_eq(response_line_glob, rule->response_line_glob);

  ck_assert_int_eq(exitcode, rule->exitcode);

  if (response == NULL) ck_assert_ptr_null(rule->response);
  else ck_assert_str_eq(response, rule->response);

}

filter_rule *parse_control_line(stralloc *control_line) {
  filter_rule *rule = (filter_rule *)alloc(sizeof(filter_rule));

  rule->next	              = NULL;
  rule->env	                = NULL;
  rule->event	              = NULL;
  rule->request_prepend	    = NULL;
  rule->response_line_glob	= NULL;
  rule->exitcode	          = 0;
  rule->response	          = NULL;

  stralloc value = {0}; stralloc_copys(&value, "");
  int fields_seen = 0;
  for (int i = 0; i < control_line->len; i++) {
    char *c = &control_line->s[i];
    if (':' == *c) {
      if (value.len) {
        stralloc_0(&value);
        if (0 == fields_seen) rule->env = value.s;
        if (1 == fields_seen) rule->event = value.s;
      }
      fields_seen++;
      stralloc_copys(&value, "");
    } else {
      stralloc_append(&value, c);
    }
  }
  if (value.len) {
    if (1 == fields_seen) rule->event = value.s;
  }

  return rule;
}

START_TEST (test_blank_line)
{
  stralloc control_line = {0};

  filter_rule *rule = parse_control_line(&control_line);

  assert_parsed_line(rule, NULL, NULL, NULL, NULL, 0, NULL);
}
END_TEST

START_TEST (test_nonblank_line)
{
  stralloc control_line = {0}; stralloc_copys(&control_line, ",");

  filter_rule *rule = parse_control_line(&control_line);

  assert_parsed_line(rule, NULL, NULL, NULL, NULL, 0, NULL);
}
END_TEST

START_TEST (test_no_env_or_event)
{
  stralloc control_line = {0}; stralloc_copys(&control_line, ":");

  filter_rule *rule = parse_control_line(&control_line);

  assert_parsed_line(rule, NULL, NULL, NULL, NULL, 0, NULL);
}
END_TEST

START_TEST (test_no_env_yes_event)
{
  stralloc control_line = {0}; stralloc_copys(&control_line, ":smtp_verb");

  filter_rule *rule = parse_control_line(&control_line);

  assert_parsed_line(rule, NULL, "smtp_verb", NULL, NULL, 0, NULL);
}
END_TEST

TCase *tc_control(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_blank_line);
  tcase_add_test(tc, test_nonblank_line);
  tcase_add_test(tc, test_no_env_or_event);
  tcase_add_test(tc, test_no_env_yes_event);

  return tc;
}

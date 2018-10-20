#include "check.h"

#include "fixsmtpio_filter.h"

filter_rule *parse_control_line(stralloc *line) {
  filter_rule *rule = (filter_rule *)alloc(sizeof(filter_rule));

  rule->next                = NULL;
  rule->env                 = NULL;
  rule->event               = NULL;
  rule->request_prepend     = NULL;
  rule->response_line_glob  = NULL;
  rule->exitcode            = EXIT_LATER_NORMALLY;
  rule->response            = NULL;

  stralloc value = {0}; stralloc_copys(&value, "");
  int fields_seen = 0;
  char *s;
  int i;
  for (i = 0; i < line->len; i++) {
    char c = line->s[i];
    if (':' == c) {
      fields_seen++;
      if (value.len) {
        stralloc_0(&value);
        s = (char *)alloc(value.len);
        str_copy(s, value.s);
        stralloc_copys(&value, "");
        switch (fields_seen) {
          case 1: rule->env                = s; break;
          case 2: rule->event              = s; break;
          case 3: rule->request_prepend    = s; break;
          case 4: rule->response_line_glob = s; break;
          case 5:
            if (!scan_ulong(s,&rule->exitcode)) return NULL;
            if (rule->exitcode > 255) return NULL;
            break;
          case 6: rule->response           = s; break;
        }
      }
    } else {
      stralloc_append(&value, &c);
    }
  }

  fields_seen++;
  if (value.len) {
    stralloc_0(&value);
    s = (char *)alloc(value.len);
    str_copy(s, value.s);
    stralloc_copys(&value, "");
    switch (fields_seen) {
      case 2: rule->event    = s; break;
      case 6: rule->response = s; break;
    }
  }

  return rule;
}

#define assert_str_null_or_eq(s1,s2) \
  if (s1 == NULL) \
    ck_assert_ptr_null(s2); \
  else \
    ck_assert_str_eq(s1, s2);

void assert_parsed_line(char *input,
                        char *env,char *event,char *request_prepend,
                        char *response_line_glob,int exitcode,char *response) {
  stralloc line = {0}; stralloc_copys(&line, input);
  filter_rule *rule = parse_control_line(&line);

  ck_assert_ptr_null(rule->next);
  assert_str_null_or_eq(env, rule->env);
  assert_str_null_or_eq(event, rule->event);
  assert_str_null_or_eq(request_prepend, rule->request_prepend);
  assert_str_null_or_eq(response_line_glob, rule->response_line_glob);
  ck_assert_int_eq(exitcode, rule->exitcode);
  assert_str_null_or_eq(response, rule->response);
}

void assert_non_parsed_line(char *input) {
  stralloc line = {0}; stralloc_copys(&line, input);
  ck_assert_ptr_null(parse_control_line(&line));
}

START_TEST (test_blank_line) {
  assert_parsed_line(
      "",
      NULL, NULL, NULL, NULL, EXIT_LATER_NORMALLY, NULL
  );
} END_TEST

START_TEST (test_nonblank_line) {
  assert_parsed_line(
      ",",
      NULL, NULL, NULL, NULL, EXIT_LATER_NORMALLY, NULL
  );
} END_TEST

START_TEST (test_no_env_or_event) {
  assert_parsed_line(
      ":",
      NULL, NULL, NULL, NULL, EXIT_LATER_NORMALLY, NULL
  );
} END_TEST

START_TEST (test_no_env_yes_event) {
  assert_parsed_line(
      ":smtp_verb",
      NULL, "smtp_verb", NULL, NULL, EXIT_LATER_NORMALLY, NULL
  );
} END_TEST

START_TEST (test_yes_env_yes_event) {
  assert_parsed_line(
      "ENV_VAR:some_verb",
      "ENV_VAR", "some_verb", NULL, NULL, EXIT_LATER_NORMALLY, NULL
  );
} END_TEST

START_TEST (test_realistic_line) {
  assert_parsed_line(
      ":word:NOOP :*::250 indeed",
      NULL, "word", "NOOP ", "*", EXIT_LATER_NORMALLY, "250 indeed");
} END_TEST

START_TEST (test_valid_exitcode) {
  assert_parsed_line(
      ":sup::*:5:250 yo",
      NULL, "sup", NULL, "*", 5, "250 yo");
} END_TEST

START_TEST (test_exitcode_too_large) {
  assert_non_parsed_line(":e::*:500:r");
} END_TEST

START_TEST (test_exitcode_non_numeric) {
  assert_non_parsed_line(":e::*:-5:r");
} END_TEST

TCase *tc_control(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_blank_line);
  tcase_add_test(tc, test_nonblank_line);
  tcase_add_test(tc, test_no_env_or_event);
  tcase_add_test(tc, test_no_env_yes_event);
  tcase_add_test(tc, test_yes_env_yes_event);
  tcase_add_test(tc, test_realistic_line);
  tcase_add_test(tc, test_valid_exitcode);
  tcase_add_test(tc, test_exitcode_too_large);
  tcase_add_test(tc, test_exitcode_non_numeric);

  return tc;
}

#include "check.h"

#include "fixsmtpio_control.h"

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

  ck_assert_ptr_nonnull(rule);
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

START_TEST (test_reject_blank_line) {
  assert_non_parsed_line(
    ""
  );
} END_TEST

START_TEST (test_reject_just_a_comma) {
  assert_non_parsed_line(
    ","
  );
} END_TEST

START_TEST (test_reject_just_a_colon) {
  assert_non_parsed_line(
    ":"
  );
} END_TEST

START_TEST (test_accept_empty_env) {
  assert_parsed_line(
    ":event:prepend:glob:55:response",
    NULL,"event","prepend","glob",55,"response"
  );
} END_TEST

START_TEST (test_reject_empty_event) {
  assert_non_parsed_line(
    "env::prepend:glob:55:response"
  );
} END_TEST

START_TEST (test_accept_empty_request_prepend) {
  assert_parsed_line(
    "env:event::glob:55:response",
    "env","event",NULL,"glob",55,"response"
  );
} END_TEST

START_TEST (test_reject_empty_response_line_glob) {
  assert_non_parsed_line(
    "env:event:prepend::55:response"
  );
} END_TEST

START_TEST (test_accept_empty_exitcode) {
  assert_parsed_line(
    "env:event:prepend:glob::response",
    "env","event","prepend","glob",EXIT_LATER_NORMALLY,"response"
  );
} END_TEST

START_TEST (test_reject_exitcode_non_numeric) {
  assert_non_parsed_line(
    "env:event:prepend:glob:exitcode:response"
  );
} END_TEST

START_TEST (test_reject_exitcode_too_large) {
  assert_non_parsed_line(
    "env:event:prepend:glob:500:response"
  );
} END_TEST

START_TEST (test_accept_valid_exitcode) {
  assert_parsed_line(
    "env:event:prepend:glob:5:response",
    "env","event","prepend","glob",5,"response"
  );
} END_TEST

START_TEST (test_accept_empty_response) {
  assert_parsed_line(
    "env:event:prepend:glob:55:",
    "env","event","prepend","glob",55,""
  );
} END_TEST

START_TEST (test_reject_response_not_specified) {
  assert_non_parsed_line(
    "env:event:prepend:glob:55"
  );
} END_TEST

START_TEST (test_accept_response_containing_space) {
  assert_parsed_line(
    "env:event:prepend:glob:55:response 250 ok",
    "env","event","prepend","glob",55,"response 250 ok"
  );
} END_TEST

START_TEST (test_accept_response_containing_colon) {
  assert_parsed_line(
    "env:event:prepend:glob:55:response: 250 ok",
    "env","event","prepend","glob",55,"response: 250 ok"
  );
} END_TEST

START_TEST (test_accept_realistic_line) {
  assert_parsed_line(
    ":word:NOOP :*::250 indeed",
    NULL,"word","NOOP ","*",EXIT_LATER_NORMALLY,"250 indeed"
  );
} END_TEST

TCase *tc_control(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_reject_blank_line);
  tcase_add_test(tc, test_reject_just_a_comma);
  tcase_add_test(tc, test_reject_just_a_colon);
  tcase_add_test(tc, test_accept_empty_env);
  tcase_add_test(tc, test_reject_empty_event);
  tcase_add_test(tc, test_accept_empty_request_prepend);
  tcase_add_test(tc, test_reject_empty_response_line_glob);
  tcase_add_test(tc, test_accept_empty_exitcode);
  tcase_add_test(tc, test_reject_exitcode_non_numeric);
  tcase_add_test(tc, test_reject_exitcode_too_large);
  tcase_add_test(tc, test_accept_valid_exitcode);
  tcase_add_test(tc, test_accept_empty_response);
  tcase_add_test(tc, test_reject_response_not_specified);
  tcase_add_test(tc, test_accept_response_containing_space);
  tcase_add_test(tc, test_accept_response_containing_colon);
  tcase_add_test(tc, test_accept_realistic_line);

  return tc;
}

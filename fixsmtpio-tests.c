#include "check.h"
#include <stdlib.h>

#include "fixsmtpio_proxy.h"
#include "fixsmtpio_filter.h"
#include "stralloc.h"

void assert_strip_last_eol(const char *input, const char *expected_output) {
  stralloc sa = {0}; stralloc_copys(&sa, input);

  strip_last_eol(&sa);

  ck_assert_int_eq(sa.len, strlen(expected_output));
  stralloc_0(&sa);
  ck_assert_str_eq(sa.s, expected_output);
}

START_TEST (test_strip_last_eol)
{
  assert_strip_last_eol("", "");
  assert_strip_last_eol("\n", "");
  assert_strip_last_eol("\r", "");
  assert_strip_last_eol("\r\n", "");
  assert_strip_last_eol("\n\r", "\n");
  assert_strip_last_eol("\r\r", "\r");
  assert_strip_last_eol("\n\n", "\n");
  assert_strip_last_eol("yo geeps", "yo geeps");
  assert_strip_last_eol("yo geeps\r\n", "yo geeps");
  assert_strip_last_eol("yo geeps\r\nhow you doin?\r\n", "yo geeps\r\nhow you doin?");
  assert_strip_last_eol("yo geeps\r\nhow you doin?", "yo geeps\r\nhow you doin?");
}
END_TEST

void assert_is_entire_line(char *input, int expected) {
  stralloc sa = {0}; stralloc_copys(&sa, input);

  int actual = is_entire_line(&sa);

  ck_assert_int_eq(actual, expected);
}

START_TEST (test_is_entire_line)
{
  // annoying to test, currently don't believe I have this bug:
  // assert_is_entire_line(NULL, 0);
  assert_is_entire_line("", 0);
  assert_is_entire_line("123", 0);
  assert_is_entire_line("123\n", 1);
  assert_is_entire_line("1\n23\n", 1);
}
END_TEST


void assert_could_be_final_response_line(const char *input, int expected)
{
  stralloc sa = {0}; stralloc_copys(&sa, input);
  int actual = could_be_final_response_line(&sa);
  ck_assert_int_eq(actual, expected); }


START_TEST (test_could_be_final_response_line)
{
  //assert_could_be_final_response_line(NULL, 0);
  assert_could_be_final_response_line("", 0);
  assert_could_be_final_response_line("123", 0);
  assert_could_be_final_response_line("1234", 0);
  assert_could_be_final_response_line("123 this is a final line", 1);
  assert_could_be_final_response_line("123-this is NOT a final line", 0);
  

  // two surprises, but maybe fine for this function's job:
  // - "\r\n" can be un-present and it's fine
  // - it can have nothing after the space and it's fine
  assert_could_be_final_response_line("123 ", 1);
  assert_could_be_final_response_line("123\n", 0);
}
END_TEST

void _assert_filter_rule(int expected, filter_rule *filter_rule, const char *event) {
  stralloc sa = {0}; stralloc_copys(&sa, event);

  int actual = filter_rule_applies(filter_rule,&sa);

  ck_assert_int_eq(actual, expected);
}

void assert_filter_rule_applies(filter_rule *filter_rule, const char *event) {
  _assert_filter_rule(1, filter_rule, event);
}

void assert_filter_rule_does_not_apply(filter_rule *filter_rule, const char *event) {
  _assert_filter_rule(0, filter_rule, event);
}

START_TEST (test_filter_rule_applies)
{
  filter_rule rule = {
    NULL,
    ENV_ANY,                  "caliente",
    REQUEST_PASSTHRU          "*",
    EXIT_LATER_NORMALLY,      ""
  };
  assert_filter_rule_does_not_apply(&rule, "clienteof");

  rule.event = "clienteof";
  assert_filter_rule_applies(&rule, "clienteof");
}
END_TEST

Suite * fixsmtpio_suite(void)
{
  Suite *s;
  TCase *tc_proxy, *tc_filter;

  s = suite_create("fixsmtpio");

  tc_proxy = tcase_create("proxy");
  tcase_add_test(tc_proxy, test_strip_last_eol);
  tcase_add_test(tc_proxy, test_is_entire_line);
  tcase_add_test(tc_proxy, test_could_be_final_response_line);
  suite_add_tcase(s, tc_proxy);

  tc_filter = tcase_create("filter");
  tcase_add_test(tc_filter, test_filter_rule_applies);
  suite_add_tcase(s, tc_filter);

  return s;
}

int main(void)
{
  int number_failed;
  Suite *s;
  SRunner *sr;

  s = fixsmtpio_suite();
  sr = srunner_create(s);

  srunner_set_tap(sr, "-");
  srunner_run_all(sr, CK_SILENT);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

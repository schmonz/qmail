#include "check.h"
#include <stdlib.h>

#include "fixsmtpio-filter.h"
#include "stralloc.h"

void assert_strip_last_eol(const char *input, const char *expected_output) {
  stralloc sa = {0};
  stralloc_copys(&sa, input);

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

void _assert_filter_rule(int should_apply, filter_rule *filter_rule, const char *event) {
  stralloc sa = {0};
  stralloc_copys(&sa, event);

  int result = filter_rule_applies(filter_rule,&sa);

  ck_assert_int_eq(result, should_apply);
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
    0,
    ENV_ANY,                  "caliente",
    PREPEND_NOTHING,          "*",
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
  TCase *tc_core;

  s = suite_create("fixsmtpio");

  tc_core = tcase_create("filter");
  tcase_add_test(tc_core, test_strip_last_eol);
  tcase_add_test(tc_core, test_filter_rule_applies);
  suite_add_tcase(s, tc_core);

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


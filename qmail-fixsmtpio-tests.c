#include "check.h"
#include <stdlib.h>

#include "qmail-fixsmtpio-filter.h"
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

Suite * fixsmtpio_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("fixsmtpio");

  /* Core test case */
  tc_core = tcase_create("Core");

  tcase_add_test(tc_core, test_strip_last_eol);
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
  //srunner_run_all(sr, CK_SILENT);
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


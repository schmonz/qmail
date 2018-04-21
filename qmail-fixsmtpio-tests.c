#include "check.h"
#include <stdlib.h>

#include "qmail-fixsmtpio-filter.h"
#include "stralloc.h"

void assert_strip_last_eol(int charsremoved, const char *input) {
	stralloc sa = {0};
  stralloc_copys(&sa, input);
  int len = sa.len;

	strip_last_eol(&sa);

	ck_assert_int_eq(len - charsremoved, sa.len);
}

START_TEST (test_strip_last_eol)
{
  assert_strip_last_eol(1, "\n");
  assert_strip_last_eol(1, "\r");
  assert_strip_last_eol(2, "\r\n");
  assert_strip_last_eol(1, "\n\r");
  assert_strip_last_eol(1, "\r\r");
  assert_strip_last_eol(1, "\n\n");
  assert_strip_last_eol(0, "yo geeps");
  assert_strip_last_eol(2, "yo geeps\r\n");
  assert_strip_last_eol(2, "yo geeps\r\nhow you doin?\r\n");
  assert_strip_last_eol(0, "yo geeps\r\nhow you doin?");
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

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


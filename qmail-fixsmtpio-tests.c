#include "check.h"
#include <stdlib.h>

#include "qmail-fixsmtpio-filter.h"
#include "stralloc.h"

START_TEST (test_strip_last_eol)
{
	stralloc sa = {0};

	ck_assert_ptr_eq(0, sa.s);
	ck_assert_int_eq(0, sa.len);
	strip_last_eol(&sa);
	ck_assert_ptr_eq(0, sa.s);
	ck_assert_int_eq(0, sa.len);

  stralloc_copys(&sa,"\n");
	ck_assert_int_eq(1, sa.len);
	strip_last_eol(&sa);
	ck_assert_int_eq(0, sa.len);
}
END_TEST

/*
 * given empty stralloc, return empty stralloc
 * given stralloc consisting of "\r", return empty stralloc
 * given stralloc consisting of "\r\n", return empty stralloc
 * given stralloc consisting of "\n\r", ???
 * given stralloc consisting of "yo geeps", return "yo geeps"
 * given stralloc consisting of "yo geeps\r\n", return "yo geeps"
 * given stralloc consisting of "yo geeps\r\nhow you doin?\r\n", return "yo geeps\r\nhow you doin?"
 */

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


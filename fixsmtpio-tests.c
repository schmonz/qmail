#include "check.h"
#include <stdlib.h>

extern TCase *tc_eventq(void);
extern TCase *tc_filter(void);
extern TCase *tc_glob(void);
extern TCase *tc_munge(void);
extern TCase *tc_proxy(void);

#include "fixsmtpio_common.h"
#include "stralloc.h"
#include "test_fixsmtpio_common.c"

Suite * fixsmtpio_suite(void)
{
  Suite *s;
  TCase *tc_common;

  s = suite_create("fixsmtpio");

  tc_common = tcase_create("common");
  tcase_add_test(tc_common, test_prepends);
  suite_add_tcase(s, tc_common);

  suite_add_tcase(s, tc_eventq());
  suite_add_tcase(s, tc_filter());
  suite_add_tcase(s, tc_glob());
  suite_add_tcase(s, tc_munge());
  suite_add_tcase(s, tc_proxy());

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

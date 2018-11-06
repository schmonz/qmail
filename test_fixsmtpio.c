#include "check.h"
#include <stdlib.h>

extern TCase *tc_stralloc(void);
extern TCase *tc_control(void);
extern TCase *tc_eventq(void);
extern TCase *tc_filter(void);
extern TCase *tc_glob(void);
extern TCase *tc_munge(void);
extern TCase *tc_proxy(void);

Suite * fixsmtpio_suite(void)
{
  Suite *s = suite_create("fixsmtpio");

  suite_add_tcase(s, tc_stralloc());
  suite_add_tcase(s, tc_control());
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

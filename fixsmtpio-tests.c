#include "check.h"
#include <stdlib.h>

#include "fixsmtpio_common.h"
#include "fixsmtpio_proxy.h"
#include "fixsmtpio_eventq.h"
#include "fixsmtpio_filter.h"
#include "stralloc.h"
#include "test_fixsmtpio_common.c"
#include "test_fixsmtpio_eventq.c"
#include "test_fixsmtpio_filter.c"
#include "test_fixsmtpio_glob.c"
#include "test_fixsmtpio_munge.c"
#include "test_fixsmtpio_proxy.c"

Suite * fixsmtpio_suite(void)
{
  Suite *s;
  TCase *tc_common, *tc_eventq, *tc_filter, *tc_glob, *tc_munge, *tc_proxy;

  s = suite_create("fixsmtpio");

  tc_common = tcase_create("common");
  tcase_add_test(tc_common, test_prepends);
  suite_add_tcase(s, tc_common);

  tc_eventq = tcase_create("eventq");
  tcase_add_test(tc_eventq, test_eventq_put_and_get);
  suite_add_tcase(s, tc_eventq);

  tc_filter = tcase_create("filter");
  tcase_add_test(tc_filter, test_filter_rule_applies);
  tcase_add_test(tc_filter, test_want_munge_internally);
  tcase_add_test(tc_filter, test_want_munge_from_config);
  tcase_add_test(tc_filter, test_envvar_exists_if_needed);
  tcase_add_test(tc_filter, test_munge_response_line);
  tcase_add_test(tc_filter, test_munge_response);
  suite_add_tcase(s, tc_filter);

  tc_glob = tcase_create("glob");
  tcase_add_test(tc_glob, test_string_matches_glob);
  suite_add_tcase(s, tc_glob);

  tc_munge = tcase_create("munge");
  tcase_add_test(tc_munge, test_change_every_line_fourth_char_to_dash);
  tcase_add_test(tc_munge, test_change_last_line_fourth_char_to_space);
  tcase_add_test(tc_munge, test_event_matches);
  suite_add_tcase(s, tc_munge);

  tc_proxy = tcase_create("proxy");
  tcase_add_test(tc_proxy, test_strip_last_eol);
  tcase_add_test(tc_proxy, test_ends_with_newline);
  tcase_add_test(tc_proxy, test_is_last_line_of_response);
  tcase_add_test(tc_proxy, test_parse_client_request);
  tcase_add_test(tc_proxy, test_get_one_response);
  suite_add_tcase(s, tc_proxy);

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

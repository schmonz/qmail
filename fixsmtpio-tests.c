#include "check.h"
#include <stdlib.h>

#include "fixsmtpio_common.h"
#include "fixsmtpio_proxy.h"
#include "fixsmtpio_eventq.h"
#include "fixsmtpio_filter.h"
#include "stralloc.h"

void assert_prepends(const char *input, const char *prepend, const char *expected_output) {
  stralloc sa = {0}; stralloc_copys(&sa, input);

  prepends(&sa, prepend);

  stralloc_0(&sa);
  ck_assert_str_eq(sa.s, expected_output);
}

START_TEST (test_prepends)
{
  assert_prepends("", "", "");
  assert_prepends("", "foo", "foo");
  assert_prepends("bar", "", "bar");
  assert_prepends("baz", "foo bar", "foo barbaz");
  assert_prepends("baz quux", "foo bar ", "foo bar baz quux");
  assert_prepends(" baz quux", "foo bar", "foo bar baz quux");
}
END_TEST

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

void assert_ends_with_newline(char *input, int expected) {
  stralloc sa = {0}; stralloc_copys(&sa, input);

  int actual = ends_with_newline(&sa);

  ck_assert_int_eq(actual, expected);
}

START_TEST (test_ends_with_newline)
{
  // annoying to test, currently don't believe I have this bug:
  // assert_ends_with_newline(NULL, 0);
  assert_ends_with_newline("", 0);
  assert_ends_with_newline("123", 0);
  assert_ends_with_newline("123\n", 1);
  assert_ends_with_newline("1\n23\n", 1);
}
END_TEST


void assert_is_last_line_of_response(const char *input, int expected)
{
  stralloc sa = {0}; stralloc_copys(&sa, input);
  int actual = is_last_line_of_response(&sa);
  ck_assert_int_eq(actual, expected); }


START_TEST (test_is_last_line_of_response)
{
  //assert_is_last_line_of_response(NULL, 0);
  assert_is_last_line_of_response("", 0);
  assert_is_last_line_of_response("123", 0);
  assert_is_last_line_of_response("1234", 0);
  assert_is_last_line_of_response("123 this is a final line", 1);
  assert_is_last_line_of_response("123-this is NOT a final line", 0);
  assert_is_last_line_of_response("777-is not\r\n", 0);
  assert_is_last_line_of_response("777 is\r\n", 1);
  

  // two surprises, but maybe fine for this function's job:
  // - "\r\n" can be un-present and it's fine
  // - it can have nothing after the space and it's fine
  assert_is_last_line_of_response("123 ", 1);
  assert_is_last_line_of_response("123\n", 0);
}
END_TEST

void assert_parse_client_request(const char *request, const char *verb, const char *arg)
{
  stralloc sa_request = {0}; stralloc_copys(&sa_request, request);
  stralloc sa_request_copy = {0}; stralloc_copy(&sa_request_copy, &sa_request);
  stralloc sa_verb = {0};
  stralloc sa_arg = {0};

  parse_client_request(&sa_verb, &sa_arg, &sa_request);

  ck_assert_int_eq(sa_request_copy.len, sa_request.len);
  stralloc_0(&sa_verb);
  ck_assert_str_eq(sa_verb.s, verb);
  stralloc_0(&sa_arg);
  ck_assert_str_eq(sa_arg.s, arg);
}

START_TEST (test_parse_client_request)
{
  //assert_parse_client_request(NULL, "", "");
  assert_parse_client_request("", "", "");
  assert_parse_client_request("MAIL FROM:<schmonz@schmonz.com>\r\n", "MAIL", "FROM:<schmonz@schmonz.com>");
  assert_parse_client_request("RCPT TO:<geepawhill@geepawhill.org>\r\n", "RCPT", "TO:<geepawhill@geepawhill.org>");
  assert_parse_client_request("GENIUSPROGRAMMER\r\n", "GENIUSPROGRAMMER", "");
  assert_parse_client_request(" NEATO\r\n", "", "NEATO");
  assert_parse_client_request("SWELL \r\n", "SWELL", "");
  assert_parse_client_request(" \r\n", "", "");
  assert_parse_client_request("   \r\n", "", "  ");
  assert_parse_client_request("SUPER WEIRD STUFF\r\n", "SUPER", "WEIRD STUFF");
  assert_parse_client_request("R WEIRD STUFF\r\n", "R", "WEIRD STUFF");
  assert_parse_client_request("MAIL FROM:<schmonz@schmonz.com>\r\nRCPT TO:<geepawhill@geepawhill.org>\r\n", "MAIL", "FROM:<schmonz@schmonz.com>\r\nRCPT TO:<geepawhill@geepawhill.org>");
}
END_TEST

static void assert_get_one_response(const char *input, const char *expected_result, const char *expected_remaining, int expected_return) {
  stralloc actual_one = {0}, actual_many = {0};
  int return_value;
  copys(&actual_many,input);

  return_value = get_one_response(&actual_one,&actual_many);

  ck_assert_int_eq(return_value, expected_return);

  stralloc_0(&actual_one);
  ck_assert_str_eq(actual_one.s, expected_result);

  stralloc_0(&actual_many);
  ck_assert_str_eq(actual_many.s, expected_remaining);
}

START_TEST (test_get_one_response)
{
  assert_get_one_response("777 oneline\r\n", "777 oneline\r\n", "", 1);
  assert_get_one_response("777 separate\r\n888 responses\r\n", "777 separate\r\n", "888 responses\r\n", 1);
  assert_get_one_response("777-two\r\n777 lines\r\n888 three\r\n", "777-two\r\n777 lines\r\n", "888 three\r\n", 1);
  assert_get_one_response("777-two\r\n777 lines\r\n888 three\r\n999 four\r\n", "777-two\r\n777 lines\r\n", "888 three\r\n999 four\r\n", 1);
  assert_get_one_response("777-two\r\n", "", "777-two\r\n", 0);
}
END_TEST

START_TEST (test_eventq_put_and_get)
{
  ck_assert_str_eq(eventq_get(), "timeout");

  eventq_put("foo");
  ck_assert_str_eq(eventq_get(), "foo");

  eventq_put("bar");
  eventq_put("baz");
  eventq_put("quux");
  ck_assert_str_eq(eventq_get(), "bar");
  ck_assert_str_eq(eventq_get(), "baz");
  ck_assert_str_eq(eventq_get(), "quux");
  ck_assert_str_eq(eventq_get(), "timeout");
}
END_TEST

void assert_filter_rule(filter_rule *filter_rule, const char *event, int expected) {
  ck_assert_int_eq(filter_rule_applies(filter_rule, event), expected);
}

START_TEST (test_filter_rule_applies)
{
  filter_rule rule = {
    NULL,
    ENV_ANY,                  "caliente",
    REQUEST_PASSTHRU          "*",
    EXIT_LATER_NORMALLY,      ""
  };
  assert_filter_rule(&rule, "clienteof", 0);

  rule.event = "clienteof";
  assert_filter_rule(&rule, "clienteof", 1);
}
END_TEST

Suite * fixsmtpio_suite(void)
{
  Suite *s;
  TCase *tc_common, *tc_proxy, *tc_eventq, *tc_filter;

  s = suite_create("fixsmtpio");

  tc_common = tcase_create("common");
  tcase_add_test(tc_common, test_prepends);
  suite_add_tcase(s, tc_common);

  tc_proxy = tcase_create("proxy");
  tcase_add_test(tc_proxy, test_strip_last_eol);
  tcase_add_test(tc_proxy, test_ends_with_newline);
  tcase_add_test(tc_proxy, test_is_last_line_of_response);
  tcase_add_test(tc_proxy, test_parse_client_request);
  tcase_add_test(tc_proxy, test_get_one_response);
  suite_add_tcase(s, tc_proxy);

  tc_eventq = tcase_create("eventq");
  tcase_add_test(tc_eventq, test_eventq_put_and_get);
  suite_add_tcase(s, tc_eventq);

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

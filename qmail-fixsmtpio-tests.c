#include "unity.h"

#include "qmail-fixsmtpio.h"

void assert_config_parse(char *line,
                         char *env,char *event,char *request_prepend,
                         char *response_line_glob,char *exitcode_str,
                         char *response) {
  stralloc config          = {0};
  int pos                  = 0;

  copys(&config,line);

  char *actual_env                = get_next_field(&pos,&config);
  char *actual_event              = get_next_field(&pos,&config);
  char *actual_request_prepend    = get_next_field(&pos,&config);
  char *actual_response_line_glob = get_next_field(&pos,&config);
  char *actual_exitcode_str       = get_next_field(&pos,&config);
  char *actual_response           = get_next_field(&pos,&config);

  TEST_ASSERT_EQUAL_STRING(env, actual_env);
  TEST_ASSERT_EQUAL_STRING(event, actual_event);
  TEST_ASSERT_EQUAL_STRING(request_prepend, actual_request_prepend);
  TEST_ASSERT_EQUAL_STRING(response_line_glob, actual_response_line_glob);
  TEST_ASSERT_EQUAL_STRING(exitcode_str, actual_exitcode_str);
  TEST_ASSERT_EQUAL_STRING(response, actual_response);
}

void test_parse_config_clienteof(void) {
  assert_config_parse("AUTHUSER:clienteof::*:0",
                      "AUTHUSER",
                      "clienteof",
                      "",
                      "*",
                      "0",
                      NULL);
}

void test_parse_config_greeting4xx(void) {
  assert_config_parse("AUTHUSER:greeting::4*:14",
                      "AUTHUSER",
                      "greeting",
                      "",
                      "4*",
                      "14",
                      NULL);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_config_clienteof);
  RUN_TEST(test_parse_config_greeting4xx);
  return UNITY_END();
}

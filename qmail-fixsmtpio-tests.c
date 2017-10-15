#include "unity.h"

#include "qmail-fixsmtpio.h"

void test_parse_config_clienteof(void) {
  stralloc config          = {0};
  int pos                  = 0;

  copys(&config,"AUTHUSER:clienteof::*:0");

  char *env                = get_next_field(&pos,&config);
  char *event              = get_next_field(&pos,&config);
  char *request_prepend    = get_next_field(&pos,&config);
  char *response_line_glob = get_next_field(&pos,&config);
  char *exitcode_str       = get_next_field(&pos,&config);
  char *response           = get_next_field(&pos,&config);

  TEST_ASSERT_EQUAL_STRING( "AUTHUSER", env);
  TEST_ASSERT_EQUAL_STRING("clienteof", event);
  TEST_ASSERT_EQUAL_STRING(         "", request_prepend);
  TEST_ASSERT_EQUAL_STRING(        "*", response_line_glob);
  TEST_ASSERT_EQUAL_STRING(        "0", exitcode_str);
  TEST_ASSERT_NULL(	                    response);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_config_clienteof);
  return UNITY_END();
}

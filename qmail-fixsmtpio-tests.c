#include "unity.h"

#include "qmail-fixsmtpio.h"

void test_nothing_interesting(void) {
  TEST_ASSERT_EQUAL_INT(7, 7);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_interesting);
  return UNITY_END();
}

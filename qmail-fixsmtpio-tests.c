#include "unity.h"

#include "qmail-fixsmtpio.c"

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void test_function_should_doBlahAndBlah(void) {
  //test stuff
}

void test_function_should_doAlsoDoBlah(void) {
  TEST_ASSERT(1 == 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_function_should_doBlahAndBlah);
  RUN_TEST(test_function_should_doAlsoDoBlah);
  return UNITY_END();
}

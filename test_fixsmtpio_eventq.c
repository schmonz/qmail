#include "check.h"

#include "fixsmtpio_eventq.h"

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

TCase *tc_eventq(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_eventq_put_and_get);

  return tc;
}

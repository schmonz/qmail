#include "check.h"

#include "fixsmtpio_common.h"

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

TCase *tc_common(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_prepends);

  return tc;
}

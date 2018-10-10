#include "check.h"

#include "fixsmtpio_glob.h"

START_TEST (test_string_matches_glob) {
  ck_assert(string_matches_glob("*", ""));
  ck_assert(string_matches_glob("*", "foob;;ar"));
  ck_assert(string_matches_glob("4*", "450 tempfail"));
  ck_assert(string_matches_glob("4*", "4"));
  ck_assert(string_matches_glob("250?STARTTLS", "250?STARTTLS"));
  ck_assert(string_matches_glob("250?AUTH*", "250 AUTH "));
  ck_assert(string_matches_glob("250?AUTH*", "250 AUTH"));
  ck_assert(string_matches_glob("250?auth*", "250 auth"));
  ck_assert(string_matches_glob("2*", "250-auth login"));
  ck_assert(string_matches_glob("250?auth*", "250-auth login"));
  ck_assert(string_matches_glob("", ""));
 
 
  ck_assert(!string_matches_glob("250 auth", "the anthology contains works by 250 authors"));
  ck_assert(!string_matches_glob("250?AUTH*", "250  AUTH"));
  ck_assert(!string_matches_glob("250?AUTH*", " 250  AUTH"));
  ck_assert(!string_matches_glob("250?AUTH*", " 250 AUTH"));
  ck_assert(!string_matches_glob("4*", "foob;;ar"));
  ck_assert(!string_matches_glob("", " 250 AUTH"));
  ck_assert(!string_matches_glob("4*", "I have eaten 450 french fries"));
}
END_TEST

TCase *tc_glob(void) {
  TCase *tc = tcase_create("");

  tcase_add_test(tc, test_string_matches_glob);

  return tc;
}

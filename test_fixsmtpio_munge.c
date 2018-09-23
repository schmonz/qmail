void assert_change_every_line_fourth_char_to_dash( char* input, char *expected_response) {
  stralloc response_sa = {0}; stralloc_copys(&response_sa, input);
  change_every_line_fourth_char_to_dash(&response_sa);
  stralloc_0(&response_sa);
  ck_assert_str_eq(response_sa.s, expected_response );

}

START_TEST (test_change_every_line_fourth_char_to_dash) {
  // annoying to test, currently don't believe I have this bug:
  // assert_change_every_line_fourth_char_to_dash(NULL, "");

  assert_change_every_line_fourth_char_to_dash("", "");

  assert_change_every_line_fourth_char_to_dash("ab", "ab");

  assert_change_every_line_fourth_char_to_dash("abc", "abc");

  assert_change_every_line_fourth_char_to_dash("abcd", "abc-");

  assert_change_every_line_fourth_char_to_dash("abcd efgh\n", "abc- efgh\n");

  assert_change_every_line_fourth_char_to_dash(
      "abcd efgh\nijk\n", "abc- efgh\nijk\n");

  assert_change_every_line_fourth_char_to_dash(
      "ijk\n"
      "abcd efgh\n",

      "ijk\n"
      "abc- efgh\n");

  assert_change_every_line_fourth_char_to_dash(
      "abcd efgh\n"
      "ijk\n"
      "bcde fghi\n",

      "abc- efgh\n"
      "ijk\n"
      "bcd- fghi\n");
}
END_TEST

void assert_change_last_line_fourth_char_to_space(char* input, char *expected_response) {
  stralloc response_sa = {0}; stralloc_copys(&response_sa, input);
  change_last_line_fourth_char_to_space(&response_sa);
  stralloc_0(&response_sa);
  ck_assert_str_eq(response_sa.s, expected_response );
}

START_TEST (test_change_last_line_fourth_char_to_space) {
  // annoying to test, currently don't believe I have this bug:
  // assert_change_last_line_fourth_char_to_space(NULL, "");

  assert_change_last_line_fourth_char_to_space("", "");

  assert_change_last_line_fourth_char_to_space("ab", "ab");

  assert_change_last_line_fourth_char_to_space("abc", "abc");

  assert_change_last_line_fourth_char_to_space("abcd", "abc ");

  assert_change_last_line_fourth_char_to_space("abcd efgh\n", "abc  efgh\n");

  assert_change_last_line_fourth_char_to_space(
      "abcd efgh\nij\n", "abcd efgh\nij\n");

  assert_change_last_line_fourth_char_to_space(
      "abcd efgh\nijk\n", "abcd efgh\nijk ");

  assert_change_last_line_fourth_char_to_space(
      "ijk\n"
      "abcd efgh\n",

      "ijk\n"
      "abc  efgh\n");

  assert_change_last_line_fourth_char_to_space(
      "abcd efgh\n"
      "ijk\n"
      "bcde fghi\n",

      "abcd efgh\n"
      "ijk\n"
      "bcd  fghi\n");
}
END_TEST

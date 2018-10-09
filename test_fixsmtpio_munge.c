void assert_change_every_line_fourth_char_to_dash( char *input, char *expected_response) {
  stralloc response_sa = {0}; stralloc_copys(&response_sa, input);
  change_every_line_fourth_char_to_dash(&response_sa);
  stralloc_0(&response_sa);
  ck_assert_str_eq(response_sa.s, expected_response);

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

void assert_change_last_line_fourth_char_to_space(char *input, char *expected_response) {
  stralloc response_sa = {0}; stralloc_copys(&response_sa, input);
  change_last_line_fourth_char_to_space(&response_sa);
  stralloc_0(&response_sa);
  ck_assert_str_eq(response_sa.s, expected_response);
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

START_TEST (test_event_matches) {
  /*
  char empty_unterminated[] = {                   };
  char       unterminated[] = {'f', 'o', 'o'      };
  char         terminated[] = {'f', 'o', 'o', '\0'};

  ck_assert(!event_matches(empty_unterminated, ""));
  ck_assert(!event_matches("", empty_unterminated));
  ck_assert( event_matches(empty_unterminated, empty_unterminated));

  ck_assert(!event_matches(unterminated, ""));
  ck_assert(!event_matches("", unterminated));
  ck_assert( event_matches(unterminated, unterminated));

  ck_assert(!event_matches(unterminated, NULL));
  ck_assert(!event_matches(NULL, unterminated));

  ck_assert( event_matches(unterminated, terminated));
  */

  ck_assert(!event_matches(NULL, ""));
  ck_assert(!event_matches("", NULL));
  ck_assert(!event_matches(NULL, NULL));
  ck_assert(!event_matches("", ""));

  ck_assert(!event_matches("foo", "bar"));
  ck_assert( event_matches("baz", "baz"));
  ck_assert( event_matches("Quux", "quuX"));
}
END_TEST

void assert_munge_line_internally(char *input, int lineno, char *greeting, char *event, char *expected_output) {
  stralloc input_sa = {0}; stralloc_copys(&input_sa, input);
  stralloc greeting_sa = {0}; stralloc_copys(&greeting_sa, greeting);
  munge_line_internally(&input_sa,lineno,&greeting_sa,event);
  stralloc_0(&input_sa);
  ck_assert_str_eq(input_sa.s, expected_output);
}

START_TEST (test_munge_line_internally) {
  assert_munge_line_internally("250 word up, kids", 0, "yo.sup.local", "word", "250 word up, kids");
  assert_munge_line_internally("250-applesauce", 0, "yo.sup.local", "ehlo", "250 yo.sup.local");
  assert_munge_line_internally("250-STARTSOMETHING", 1, "yo.sup.local", "ehlo", "250-STARTSOMETHING");
  assert_munge_line_internally("250 ENDSOMETHING", 2, "yo.sup.local", "ehlo", "250 ENDSOMETHING");
  assert_munge_line_internally("250 applesauce", 0, "yo.sup.local", "helo", "250 yo.sup.local");
  assert_munge_line_internally("214 ask your grandmother\r\n", 0, "yo.sup.local", "help", "214 fixsmtpio home page: https://schmonz.com/qmail/acceptutils\r\n214 ask your grandmother\r\n");
  assert_munge_line_internally("221 get outta here", 0, "yo.sup.local", "quit", "221 yo.sup.local");
}
END_TEST

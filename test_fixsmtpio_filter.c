void assert_filter_rule(filter_rule *filter_rule, const char *event, int expected) {
  ck_assert_int_eq(filter_rule_applies(filter_rule, event), expected);
}

START_TEST (test_filter_rule_applies)
{
  filter_rule rule = {
    .next = NULL,
    .env = ENV_ANY,
    .event = "caliente",
    .request_prepend = REQUEST_PASSTHRU,
    .response_line_glob = "*",
    .exitcode = EXIT_LATER_NORMALLY,
    .response = "",
  };
  assert_filter_rule(&rule, "clienteof", 0);

  rule.event = "clienteof";
  assert_filter_rule(&rule, "clienteof", 1);
}
END_TEST

START_TEST (test_want_munge_internally) {
  ck_assert_int_eq(1, want_munge_internally("&fixsmtpio"));
  ck_assert_int_eq(0, want_munge_internally("&nofixsmtpio"));
  ck_assert_int_eq(0, want_munge_internally(""));
  //ck_assert_int_eq(0, want_munge_internally(NULL));
  ck_assert_int_eq(0, want_munge_internally("random other text\r\n"));
}
END_TEST

START_TEST (test_want_munge_from_config) {
  ck_assert_int_eq(0, want_munge_from_config("&nofixsmtpio"));
  ck_assert_int_eq(1, want_munge_from_config("&fixsmtpio"));
  ck_assert_int_eq(1, want_munge_from_config(""));
  //ck_assert_int_eq(1, want_munge_from_config(NULL));
  ck_assert_int_eq(1, want_munge_from_config("random other text\r\n"));
}
END_TEST

START_TEST (test_envvar_exists_if_needed) {
  ck_assert_int_eq(0, envvar_exists_if_needed("VERY_UNLIKELY_TO_BE_SET"));
  ck_assert_int_eq(1, envvar_exists_if_needed(""));
  ck_assert_int_eq(1, envvar_exists_if_needed(NULL));
}
END_TEST

void assert_munge_response_line(char *expected_output, int lineno, char *line, int exitcode, char *greeting, filter_rule *rules, char *event) {
  stralloc line_sa = {0}; stralloc_copys(&line_sa, line);
  stralloc greeting_sa = {0}; stralloc_copys(&greeting_sa, greeting);

  munge_response_line(lineno, &line_sa, &exitcode, &greeting_sa, rules, event);

  stralloc_0(&line_sa);

  ck_assert_str_eq(line_sa.s, expected_output);
}

START_TEST (test_munge_response_line) {

  filter_rule *rules = 0;
  assert_munge_response_line("222 sup duuuude\r\n", 0, "222 sup duuuude", 0, "yo.sup.local", rules, "ehlo");
  assert_munge_response_line("222 OUTSTANDING\r\n", 1, "222 OUTSTANDING", 0, "yo.sup.local", rules, "ehlo");

  rules = prepend_rule(rules, ENV_ANY, "helo", REQUEST_PASSTHRU, "2*", EXIT_LATER_NORMALLY, MUNGE_INTERNALLY);
  assert_munge_response_line("222 sup duuuude\r\n", 0, "222 sup duuuude", 0, "yo.sup.local", rules, "ehlo");
  assert_munge_response_line("222 OUTSTANDING\r\n", 1, "222 OUTSTANDING", 0, "yo.sup.local", rules, "ehlo");

  rules = prepend_rule(rules, ENV_ANY, "ehlo", REQUEST_PASSTHRU, "2*", EXIT_LATER_NORMALLY, MUNGE_INTERNALLY);
  assert_munge_response_line("250 yo.sup.local\r\n", 0, "222 sup duuuude", 0, "yo.sup.local", rules, "ehlo");
  assert_munge_response_line("222 OUTSTANDING\r\n", 1, "222 OUTSTANDING", 0, "yo.sup.local", rules, "ehlo");
}
END_TEST

void assert_munge_response(char *expected_output, char *response, int exitcode, char *greeting, filter_rule *rules, char *event) {
  stralloc response_sa = {0}; stralloc_copys(&response_sa, response);
  stralloc greeting_sa = {0}; stralloc_copys(&greeting_sa, greeting);

  munge_response(&response_sa, &exitcode, &greeting_sa, rules, event);

  stralloc_0(&response_sa);

  ck_assert_str_eq(response_sa.s, expected_output);
}

START_TEST (test_munge_response) {
  filter_rule *rules = 0;

  // annoying to test NULL, unlikely to be bug
  assert_munge_response("", "", EXIT_LATER_NORMALLY, "yo.sup.local", rules, "ehlo");
  assert_munge_response("512 grump\r\n", "512 grump", EXIT_LATER_NORMALLY, "yo.sup.local", rules, "ehlo");
  assert_munge_response("512-grump\r\n256 mump\r\n", "512 grump\r\n256 mump", EXIT_LATER_NORMALLY, "yo.sup.local", rules, "ehlo");
}
END_TEST
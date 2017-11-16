#include <check.h>
#include <stdlib.h>
#include <stdio.h>

#include "../qmail-fixsmtpio.h"

// get_next_field

START_TEST(it_gets_the_next_field)
{
    stralloc a = {0};
    
    a.len = 16;

    a.s = "abc:def:ghi:jkl";
    
    int index = 0;
    
    char *actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "abc");

}
END_TEST

START_TEST(it_gets_all_of_the_fields)
{
    stralloc a = {0};
    
    a.len = 23;

    a.s = "AUTHUSER:timeout::*:16:";
    
    int index = 0;
    
    char *actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "AUTHUSER");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "timeout");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, ""); 

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "*");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "16");

    actual = get_next_field(&index, &a);

    ck_assert_ptr_eq(actual, NULL);

}
END_TEST

START_TEST(it_gets_empty_fields_too)
{
    
    stralloc a = {0};
    
    a.len = 23;

    a.s = "AUTHUSER:clienteof::*:0";
    
    int index = 0;
    
    char *actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "AUTHUSER");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "clienteof");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "*");

    actual = get_next_field(&index, &a);

    ck_assert_str_eq(actual, "0");
}
END_TEST

// load_filter_rules

START_TEST(it_loads_a_configfile)
{
    filter_rule *rules = load_filter_rules("files/config");

    ck_assert_ptr_ne(rules, NULL);
    ck_assert_str_eq(rules->env, "AUTHUSER");
    ck_assert_str_eq(rules->event, "clienteof");
    ck_assert_ptr_eq(rules->request_prepend, NULL);
    ck_assert_str_eq(rules->response_line_glob, "*");
    ck_assert_ptr_eq(rules->response, NULL);
    ck_assert_ptr_eq(rules->exitcode, NULL);

    rules = rules->next;

    ck_assert_ptr_ne(rules, NULL);
    ck_assert_str_eq(rules->env, "AUTHUSER");
    ck_assert_str_eq(rules->event, "greeting");
    ck_assert_ptr_eq(rules->request_prepend, NULL);
    ck_assert_str_eq(rules->response_line_glob, "4*");
    ck_assert_int_eq(rules->exitcode, 14);
    ck_assert_ptr_eq(rules->response, NULL);

}
END_TEST

START_TEST(get_last_field)
{
    stralloc a = {0};
    
    a.len = 24;

    a.s = "AUTHUSER:timeout::*:16:";

    int index = 23;

    char *actual = get_next_field(&index, &a);

    ck_assert_ptr_eq(actual, NULL);
    
}
END_TEST

Suite *make_unit_test_suite()
{
    Suite *s;
    TCase *tc;

    s = suite_create("Qmail Fix SMTP IO");
    tc = tcase_create("Core");

    tcase_add_test(tc, it_gets_the_next_field);
    tcase_add_test(tc, it_gets_all_of_the_fields);
    tcase_add_test(tc, it_gets_empty_fields_too);
    tcase_add_test(tc, it_loads_a_configfile);
    tcase_add_test(tc, get_last_field);

    suite_add_tcase(s, tc);

    return s;
}

int main()
{
    int number_failed;
    SRunner *sr;

    sr = srunner_create(make_unit_test_suite());
    srunner_set_fork_status (sr, CK_NOFORK);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

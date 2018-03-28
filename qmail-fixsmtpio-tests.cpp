#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"

extern "C" {
    #include "qmail-fixsmtpio-filter.h"
    #include "stralloc.h"
}

TEST_GROUP(FilterStuff)
{
};

TEST(FilterStuff, StripLastEol)
{
    stralloc sa = {0};

    LONGS_EQUAL(0, sa.s);
    LONGS_EQUAL(0, sa.len);
    strip_last_eol(&sa);
    LONGS_EQUAL(0, sa.s);
    LONGS_EQUAL(0, sa.len);
/*
 * given empty stralloc, return empty stralloc
 * given stralloc consisting of "\n", return empty stralloc
 * given stralloc consisting of "\r", return empty stralloc
 * given stralloc consisting of "\r\n", return empty stralloc
 * given stralloc consisting of "\n\r", ???
 * given stralloc consisting of "yo geeps", return "yo geeps"
 * given stralloc consisting of "yo geeps\r\n", return "yo geeps"
 * given stralloc consisting of "yo geeps\r\nhow you doin?\r\n", return "yo geeps\r\nhow you doin?"
 */
}

int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}

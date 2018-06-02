#include "fixsmtpio_glob.h"

/*
 don't test fnmatch(), document what control/fixsmtpio needs from it

 glob is "*"
 glob is "4*"
 glob is "5*"
 glob is "2*"
 glob is "250?AUTH*"
 glob is "250?auth*"
 glob is "250?STARTTLS"

 string is empty
 string is "450 tempfail"
 string is "I have eaten 450 french fries"
 string is "250-auth login"
 string is "the anthology contains works by 250 authors"

 maybe this should be regex instead of glob?
 */
int string_matches_glob(char *glob,char *string) {
  return 0 == fnmatch(glob,string,0);
}

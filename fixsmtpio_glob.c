#include "fixsmtpio_glob.h"

/*
 maybe this should be regex instead of glob?
 */
int string_matches_glob(char *glob,char *string) {
  return 0 == fnmatch(glob,string,0);
}

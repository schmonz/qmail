#include <unistd.h>
#include "alloc.h"
#include "control.h"
#include "str.h"
#include "stralloc.h"
#include "wait.h"

static void unable_to_allocate() { _exit(51); }
static void unable_to_execute()  { _exit(71); }
static void unable_to_verify()   { _exit(55); }

static int num_lines(stralloc *lines) {
  int num = 0;
  int i;
  for (i = 0; i < lines->len; i++) if (lines->s[i] == '\0') num++;
  return num;
}

static void run_qmail_qfilter(stralloc *filters) {
  int num_args;
  char **args;
  int arg;
  int linestart;
  int i;

  num_args = 2 * num_lines(filters);
  if (num_args == 0) num_args = 1;
  if (!(args = (char **) alloc(sizeof(char *) * num_args)))
    unable_to_allocate();

  args[0] = "bin/qmail-qfilter";

  arg = 0;
  linestart = 0;
  for (i = 0; i < filters->len; i++) {
    if (filters->s[i] == '\0') {
      stralloc filter = {0};
      stralloc_copys(&filter, filters->s + linestart);
      stralloc_0(&filter);
      args[++arg] = filter.s;
      args[++arg] = "--";
      linestart = i + 1;
    }
  }
  args[num_args] = 0;

  execv(*args, args);
  unable_to_execute();
}

int main(int argc, char **argv) {
  stralloc filters = {0};

  if (control_readfile(&filters,"control/smtpfilters",0) == -1)
    unable_to_verify();

  run_qmail_qfilter(&filters);
}

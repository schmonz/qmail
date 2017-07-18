#include <unistd.h>
#include "control.h"
#include "stralloc.h"
#include "wait.h"

void accept_recipient() { _exit(  0); }
void reject_recipient() { _exit(100); }
void unable_to_verify() { _exit(111); }
void unable_to_execute(){ _exit(120); }

static void run_rcptcheck(char *program)
{
  char *rcptcheck[2] = { program, 0 };
  int pid;
  int wstat;

  switch(pid = fork()) {
    case -1:
      unable_to_execute();
    case 0:
      execv(*rcptcheck,rcptcheck);
      unable_to_execute();
  }

  if (wait_pid(&wstat,pid) == -1)
    unable_to_execute();
  if (wait_crashed(wstat))
    unable_to_execute();

  switch(wait_exitcode(wstat)) {
    case 100: reject_recipient();
    case 111: unable_to_verify();
    case 120: unable_to_execute();
    default:  return;
  }
}

int main(void)
{
  stralloc rcptchecks = {0};
  int linestart;
  int pos;

  if (control_readfile(&rcptchecks,"control/rcptchecks",0) == -1)
    unable_to_verify();

  for (linestart = 0, pos = 0; pos < rcptchecks.len; pos++) {
    if (rcptchecks.s[pos] == '\0') {
      run_rcptcheck(rcptchecks.s + linestart);
      linestart = pos + 1;
    }
  }

  accept_recipient();
}

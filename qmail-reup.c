#include "sig.h"

void die() { _exit(1); }

int main(int argc,char **argv) {
  sig_alarmcatch(die);
  sig_pipeignore();
 
  char **childargs = argv + 1;
  if (!*childargs) die();

  execvp(*childargs,childargs);
  die();
}

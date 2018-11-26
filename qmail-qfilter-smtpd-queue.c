#include <unistd.h>
#include "env.h"

static void unable_to_execute()  { _exit(71); }
static void unable_to_verify()   { _exit(55); }

int main(int argc, char **argv) {
  char *qqqargs[] = { "bin/qmail-qfilter-queue", 0 };

  if (!env_put("QMAILQUEUEFILTERS=control/smtpfilters"))
    unable_to_verify();

  execv(*qqqargs, qqqargs);
  unable_to_execute();
}

#include <unistd.h>
#include "env.h"
#include "substdio.h"

static void unable_to_execute()  { _exit(71); }
static void unable_to_verify()   { _exit(55); }

static char errbuf[SUBSTDIO_OUTSIZE];
static substdio sserr = SUBSTDIO_FDBUF(write,2,errbuf,sizeof(errbuf));

int main(int argc, char **argv) {
  char *qqqargs[] = { "bin/qmail-qfilter-queue", 0 };

  substdio_puts(&sserr,argv[0]);
  substdio_puts(&sserr," is deprecated. Please set\n");
  substdio_puts(&sserr,"    QMAILQUEUE=\"qmail-qfilter-queue\"\n");
  substdio_puts(&sserr,"    QMAILQUEUEFILTERS=\"control/smtpfilters\"\n");
  substdio_flush(&sserr);

  if (!env_put("QMAILQUEUEFILTERS=control/smtpfilters"))
    unable_to_verify();

  execv(*qqqargs, qqqargs);
  unable_to_execute();
}

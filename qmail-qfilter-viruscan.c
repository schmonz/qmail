#include <unistd.h>
#include "substdio.h"
#include "viruscan.h"

static void accept_message() { _exit( 0); }
static void reject_message() {
#ifdef QMAIL_QUEUE_CUSTOM_ERROR
  char buf[SUBSTDIO_OUTSIZE];
  substdio ssmsg;
  substdio_fdbuf(&ssmsg,write,6,buf,sizeof(buf));
  substdio_putsflush(&ssmsg,"Dwe don't accept email with such content (#5.3.4)");
  _exit(82);
#else
  _exit(31);
#endif
}

void die_control()           { _exit(55); }
void die_nomem()             { _exit(51); }

int main(void)
{
  if (viruscan_reject_attachment())
    reject_message();

  accept_message();
}

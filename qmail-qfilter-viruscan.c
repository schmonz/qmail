#include <unistd.h>
#include "viruscan.h"

static void accept_message() { _exit( 0); }
static void reject_message() { _exit(31); }

void die_control()           { _exit(55); }
void die_nomem()             { _exit(51); }

int main(void)
{
  if (viruscan_reject_attachment())
    reject_message();

  accept_message();
}

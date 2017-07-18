#include <unistd.h>
#include "env.h"
#include "qregexrcptto.h"

void accept_recipient() { _exit(  0); }
void reject_recipient() { _exit(100); }
void unable_to_verify() { _exit(111); }

void die_control() { unable_to_verify(); }
void die_nomem()   { unable_to_verify(); }

int main(void)
{
  char *helohost  = env_get("HELOHOST");
  char *sender    = env_get("SENDER");
  char *recipient = env_get("RECIPIENT");

  if (!sender || !recipient)
    unable_to_verify();

  if (helohost && qregex_reject_helohost(helohost))
    reject_recipient();
  if (qregex_reject_sender(sender))
    reject_recipient();
  if (qregex_reject_recipient(recipient))
    reject_recipient();

  accept_recipient();
}

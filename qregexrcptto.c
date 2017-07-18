#include <unistd.h>
#include "control.h"
#include "env.h"
#include "fmt.h"
#include "qregex.h"
#include "str.h"
#include "stralloc.h"
#include "strerr.h"

extern void die_control();
extern void die_nomem();

static stralloc matchedregex = {0};

static int _qregex_at_least_one_match(stralloc haystack, char *needle)
{
  int i = 0;
  int j = 0;
  int x = 0;
  int negate = 0;
  stralloc bmb = {0};
  stralloc curregex = {0};

  if (!stralloc_copy(&bmb,&haystack)) die_nomem();

  while (j < bmb.len) {
    i = j;
    while ((i < bmb.len) && (bmb.s[i] != '\0')) i++;
    if (bmb.s[j] == '!') {
      negate = 1;
      j++;
    }
    if (!stralloc_copyb(&curregex,bmb.s + j,(i - j))) die_nomem();
    if (!stralloc_0(&curregex)) die_nomem();
    x = matchregex(needle, curregex.s);
    if ((negate) && (x == 0)) {
      if (!stralloc_copyb(&matchedregex,bmb.s + j - 1,(i - j + 1))) die_nomem();
      if (!stralloc_0(&matchedregex)) die_nomem();
      return 1;
    }
    if (!(negate) && (x > 0)) {
      if (!stralloc_copyb(&matchedregex,bmb.s + j,(i - j))) die_nomem();
      if (!stralloc_0(&matchedregex)) die_nomem();
      return 1;
    }
    j = i + 1;
    negate = 0;
  }
  return 0;
}

static void _qregex_log_rejection(char *type, char *addr)
{
  char smtpdpid[32];
  char *remoteip = env_get("TCPREMOTEIP");
  stralloc message = {0};

  str_copy(smtpdpid + fmt_ulong(smtpdpid,getppid())," ");
  if (!remoteip) remoteip = "unknown";

  stralloc_copys(&message,"rcptcheck: qregex ");
  stralloc_cats(&message,smtpdpid);
  stralloc_cats(&message,remoteip);
  stralloc_cats(&message," ");
  stralloc_cats(&message,addr);
  stralloc_cats(&message," (");
  stralloc_cats(&message,type);
  if (env_get("LOGREGEX")) {
    stralloc_cats(&message," pattern: ");
    stralloc_cats(&message,matchedregex.s);
  }
  stralloc_cats(&message,")");
  stralloc_0(&message);

  strerr_warn1(message.s,0);
}

static int _qregex_reject_string(char *type, char *control, char *string)
{
  stralloc regexes = {0};
  int have_some_regexes = control_readfile(&regexes,control,0);
  if (have_some_regexes == -1) die_control();

  if (have_some_regexes && _qregex_at_least_one_match(regexes,string)) {
    _qregex_log_rejection(type,string);
    return 1;
  }

  return 0;
}

int qregex_reject_helohost(char *host)
{
  if (!env_get("NOBADHELO")
      && _qregex_reject_string("badhelo","control/badhelo",host))
    return 1;

  return 0;
}

int qregex_reject_recipient(char *to)
{
  if (_qregex_reject_string("badmailto","control/badmailto",to))
    return 1;

  if (!env_get("RELAYCLIENT")
      && _qregex_reject_string("badmailto","control/badmailtonorelay",to))
    return 1;

  return 0;
}

int qregex_reject_sender(char *from)
{
  if ('\0' == from[0])
    return 0;

  if (_qregex_reject_string("badmailfrom","control/badmailfrom",from))
    return 1;

  if (!env_get("RELAYCLIENT")
      && _qregex_reject_string("badmailfrom","control/badmailfromnorelay",from))
    return 1;

  return 0;
}

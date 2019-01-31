#include "alloc.h"
#include "auto_qmail.h"
#include "case.h"
#include "control.h"
#include "env.h"
#include "fd.h"
#include "scan.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

#define HOMEPAGE                 "https://schmonz.com/qmail/acceptutils"
#define PROGNAME                 "fixsmtpio"

#define EVENT_GREETING           "greeting"
#define EVENT_TIMEOUT            "timeout"
#define EVENT_CLIENTEOF          "clienteof"
#define MUNGE_INTERNALLY         "&" PROGNAME "_fixup"
#define REQUEST_PASSTHRU         ""
#define REQUEST_NOOP             "NOOP "

#define RESPONSELINE_NOCHANGE    "&" PROGNAME "_noop"

#define BEGIN_STARTTLS_NOW       -2
#define EXIT_LATER_NORMALLY      -1
#define EXIT_NOW_SUCCESS         0
#define EXIT_NOW_TEMPFAIL        14
#define EXIT_NOW_PERMFAIL        15
/* sync with authup.c */
#define EXIT_NOW_TIMEOUT         16
#define EXIT_NOW_PARSEFAIL       18

#include "env.h"
#include "fmt.h"
#include "readwrite.h"
#include "scan.h"
#include "sgetopt.h"
#include "str.h"
#include "substdio.h"
#include "wait.h"

#include "acceptutils_unistd.h"

#define PROGNAME "reup"

static void die() { unistd_exit(1); }

static char sserrbuf[SUBSTDIO_OUTSIZE];
static substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

static void out(char *s) { substdio_puts(&sserr,s); }

static void dieerrflush(char *s) {
  out(PROGNAME ": "); out(s);
  substdio_putsflush(&sserr,"\n");
  die();
}

static void die_usage() { dieerrflush("usage: " PROGNAME " [ -t tries ] prog"); }
static void die_fork()  { dieerrflush("unable to fork"); }
static void die_nomem() { dieerrflush("out of memory"); }

static char intstr[FMT_ULONG];
static void format_intstr(int myint) {
  str_copy(intstr + fmt_ulong(intstr,myint),"");
}

static void logtry(int mypid,char *childprogname,int childpid,
                   char *attempt,int maxtries,int exitcode) {
  out(PROGNAME);
  format_intstr(mypid);
  out(" ");  out(intstr);
  out(" ");  out(childprogname);
  format_intstr(childpid);
  out(" ");  out(intstr);
  out(" ("); out(attempt);
  format_intstr(maxtries);
  out("/");  out(intstr);
  out("):"); out(" exit");
  format_intstr(exitcode);
  out(" ");  out(intstr);
  substdio_putsflush(&sserr,"\n");
}

static int do_try(int attempt,int maxtries,char **childargs) {
  int childpid;
  int wstat;
  int exitcode;
  char reup[FMT_ULONG];

  str_copy(reup + fmt_ulong(reup,attempt),"");

  switch ((childpid = unistd_fork())) {
    case -1:
      die_fork();
    case 0:
      if (!env_put2("REUP",reup)) die_nomem();
      unistd_execvp(*childargs,childargs);
      die(); // log something I guess
  }

  if (wait_pid(&wstat,childpid) == -1) die(); //die why? log something
  if (wait_crashed(wstat)) die(); //die why? log something

  exitcode = wait_exitcode(wstat);
  logtry(unistd_getpid(),childargs[0],childpid,reup,maxtries,exitcode);

  return exitcode;
}

static int keep_trying(int attempt,int max) {
  if (max == 0) return 1;
  if (attempt <= max) return 1;
  return 0;
}

static int stop_trying(int exitcode) {
  switch (exitcode) {
    case 0:  return 1;
    case 12: return 1;
    default: return 0;
  }
}

int main(int argc,char **argv) {
  int exitcode;
  int opt;
  int tries;
  int i;

  tries = 0;
  while ((opt = getopt(argc,argv,"t:")) != opteof) {
    switch (opt) {
      case 't':
        if (!scan_ulong(optarg,&tries)) die_usage();
        break;
      default:
        die_usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (!*argv) die_usage();

  for (i = 1; keep_trying(i,tries); i++) {
    exitcode = do_try(i,tries,argv);
    if (stop_trying(exitcode)) unistd_exit(exitcode);
  }

  unistd_exit(exitcode);
}

#include "readwrite.h"
#include "substdio.h"
#include "exit.h"
#include "commands.h"

/* START inline commands.c */
#include "stralloc.h"
#include "case.h"
#include "str.h"
static stralloc cmd = {0};

int commands(substdio *ss,struct commands *c) {
  int i;
  char *arg;

  for (;;) {
    if (!stralloc_copys(&cmd,"")) return -1;

    for (;;) {
      if (!stralloc_readyplus(&cmd,1)) return -1;
      i = substdio_get(ss,cmd.s + cmd.len,1);
      if (i != 1) return i;
      if (cmd.s[cmd.len] == '\n') break;
      ++cmd.len;
    }

    if (cmd.len > 0) if (cmd.s[cmd.len - 1] == '\r') --cmd.len;

    cmd.s[cmd.len] = 0;

    i = str_chr(cmd.s,' ');
    arg = cmd.s + i;
    while (*arg == ' ') ++arg;
    cmd.s[i] = 0;

    for (i = 0;c[i].text;++i) if (case_equals(c[i].text,cmd.s)) break;
    c[i].fun(arg);
    if (c[i].flush) c[i].flush();
  }
}
/* END inline commands.c */

char ssinbuf[1024];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

char ssoutbuf[512];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

void flush() { substdio_flush(&ssout); }
void out(s) char *s; { substdio_puts(&ssout,s); }

void die_read() { _exit(1); }
void die_nomem() { out("421 out of memory (#4.3.0)\r\n"); flush(); _exit(1); }

void fixsmtpio_default(arg) char *arg; {
  out(cmd.s); out(" "); out(arg); out("\r\n");
}

void fixsmtpio_test(arg) char *arg; {
  out("250 qmail-fixsmtpio: "); out(arg); out("\r\n");
}

void fixsmtpio_quit_until_default_talks_to_smtpd(arg) char *arg; {
  out("221 "); out(arg); out("\r\n");
  flush();
  _exit(0);
}

struct commands smtpcommands[] = {
  { "test", fixsmtpio_test, flush }
, { "quit", fixsmtpio_quit_until_default_talks_to_smtpd, flush }
, { 0, fixsmtpio_default, flush }
} ;

int main(void) {
  if (commands(&ssin,&smtpcommands) == 0) die_read();
  die_nomem();
}

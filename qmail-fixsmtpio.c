#include "readwrite.h"
#include "substdio.h"
#include "exit.h"
#include "commands.h"

char ssinbuf[1024];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

char ssoutbuf[512];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

void flush() { substdio_flush(&ssout); }
void out(s) char *s; { substdio_puts(&ssout,s); }

void die_read() { _exit(1); }
void die_nomem() { out("421 out of memory (#4.3.0)\r\n"); flush(); _exit(1); }

void fixsmtpio_default(arg) char *arg; {
  out("502 unimplemented (#5.5.1)\r\n");
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

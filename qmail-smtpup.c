#include "commands.h"
#include "fd.h"
#include "sig.h"
#include "stralloc.h"
#include "substdio.h"
#include "alloc.h"
#include "wait.h"
#include "str.h"
#include "byte.h"
#include "now.h"
#include "fmt.h"
#include "exit.h"
#include "readwrite.h"
#include "timeoutread.h"
#include "timeoutwrite.h"
#include "base64.h"
#include "case.h"
#include "env.h"

void die() { _exit(1); }

int saferead(fd,buf,len) int fd; char *buf; int len;
{
  int r;
  r = timeoutread(1200,fd,buf,len);
  if (r <= 0) die();
  return r;
}

int safewrite(fd,buf,len) int fd; char *buf; int len;
{
  int r;
  r = timeoutwrite(1200,fd,buf,len);
  if (r <= 0) die();
  return r;
}

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(safewrite,1,ssoutbuf,sizeof ssoutbuf);

char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(saferead,0,ssinbuf,sizeof ssinbuf);

void puts(s) char *s;
{
  substdio_puts(&ssout,s);
}
void flush()
{
  substdio_flush(&ssout);
}
void err(s) char *s;
{
  puts(s);
  puts("\r\n");
  flush();
}

void die_usage() { puts("usage: qmail-smtpup hostname subprogram\n"); flush(); die(); }
void die_nomem() { err("451 qmail-smtpup out of memory (#4.3.0)"); die(); }
void die_pipe() { err("454 qmail-smtpup unable to open pipe (#4.3.0)"); die(); }
void die_write() { err("454 qmail-smtpup unable to write pipe (#4.3.0)"); die(); }
void die_fork() { err("454 qmail-smtpup unable to fork (#4.3.0)"); die(); }
void die_childcrashed() { err("454 qmail-smtpup aack, child crashed (#4.3.0)"); die(); }
void die_badauth() { err("535 qmail-smtpup authorization failed (#5.7.0)"); die(); }
void die_noauth() { err("504 qmail-smtpup auth type unimplemented (#5.5.1)"); die(); }
void die_input() { err("501 qmail-smtpup malformed auth input (#5.5.4)"); die(); }
void die_authabrt() { err("501 qmail-smtpup auth exchange cancelled (#5.0.0)"); die(); }

void err_authoriz() { err("530 qmail-smtpup Authentication required (#5.7.1)"); }

char *hostname;
stralloc username = {0};
char **childargs;
substdio ssup;
char upbuf[128];

void smtp_quit() { puts("221 "); err(hostname); die(); }

static stralloc authin = {0};
static stralloc user = {0};
static stralloc pass = {0};
static stralloc resp = {0};

void doanddie(void)
{
  int child;
  int wstat;
  int pi[2];
 
  if (fd_copy(2,1) == -1) die_pipe();
  close(3);
  if (pipe(pi) == -1) die_pipe();
  if (pi[0] != 3) die_pipe();
  switch(child = fork()) {
    case -1:
      die_fork();
    case 0:
      close(pi[1]);
      sig_pipedefault();
      if (!env_put2("SMTPUSER",user.s)) die_nomem();
      execvp(*childargs,childargs);
      _exit(1);
  }
  close(pi[0]);
  substdio_fdbuf(&ssup,write,pi[1],upbuf,sizeof upbuf);
  if (substdio_put(&ssup,user.s,user.len) == -1) die_write();
  if (substdio_put(&ssup,pass.s,pass.len) == -1) die_write();
  if (substdio_flush(&ssup) == -1) die_write();
  close(pi[1]);
  byte_zero(upbuf,sizeof upbuf);
  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die_childcrashed();
  if (wait_exitcode(wstat)) die_badauth();
  die();
}

void smtp_greet()
{
  puts("220 ");
  puts(hostname);
  puts(" ESMTP\r\n");
  flush();
}

void smtp_helo(arg) char *arg;
{
  puts("250 ");
  err(hostname);
}

void smtp_ehlo(arg) char *arg;
{
  puts("250-");
  puts(hostname);
  puts("\r\n250-AUTH LOGIN PLAIN");
  puts("\r\n250-AUTH=LOGIN PLAIN");
  err("\r\n250-PIPELINING\r\n250 8BITMIME");
}

void authgetl(void)
{
  int i;

  if (!stralloc_copys(&authin,"")) die_nomem();
  for (;;) {
    if (!stralloc_readyplus(&authin,1)) die_nomem(); /* XXX */
    i = substdio_get(&ssin,authin.s + authin.len,1);
    if (i != 1) die();
    if (authin.s[authin.len] == '\n') break;
    ++authin.len;
  }

  if (authin.len > 0) if (authin.s[authin.len - 1] == '\r') --authin.len;
  authin.s[authin.len] = 0;
  if (*authin.s == '*' && *(authin.s + 1) == 0) die_authabrt();
  if (authin.len == 0) die_input();
}

void auth_login(arg) char *arg;
{
  int r;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&user)) == 1) die_input();
  }
  else {
    err("334 VXNlcm5hbWU6"); /* Username: */
    authgetl();
    if ((r = b64decode(authin.s,authin.len,&user)) == 1) die_input();
  }
  if (r == -1) die_nomem();

  err("334 UGFzc3dvcmQ6"); /* Password: */

  authgetl();
  if ((r = b64decode(authin.s,authin.len,&pass)) == 1) die_input();
  if (r == -1) die_nomem();

  if (!user.len || !pass.len) die_input();
  if (!stralloc_0(&user)) die_nomem();
  if (!stralloc_0(&pass)) die_nomem();
  doanddie();
}

void auth_plain(arg) char *arg;
{
  int r, id = 0;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&resp)) == 1) die_input();
  }
  else {
    err("334 ");
    authgetl();
    if ((r = b64decode(authin.s,authin.len,&resp)) == 1) die_input();
  }
  if (r == -1 || !stralloc_0(&resp)) die_nomem();
  while (resp.s[id]) id++; /* ignore authorize-id */

  if (resp.len > id + 1)
    if (!stralloc_copys(&user,resp.s + id + 1)) die_nomem();
  if (resp.len > id + user.len + 2)
    if (!stralloc_copys(&pass,resp.s + id + user.len + 2)) die_nomem();

  if (!user.len || !pass.len) die_input();
  if (!stralloc_0(&user)) die_nomem();
  if (!stralloc_0(&pass)) die_nomem();
  doanddie();
}

void smtp_auth(arg) char *arg;
{
  int i;
  char *cmd = arg;

  i = str_chr(cmd,' ');
  arg = cmd + i;
  while (*arg == ' ') ++arg;
  cmd[i] = 0;

  if (case_equals("login",cmd)) auth_login(arg);
  if (case_equals("plain",cmd)) auth_plain(arg);
  die_noauth();
}

void smtp_help() { err("214 qmail-smtpup home page: https://schmonz.com/qmail/authutils"); }

struct commands smtpcommands[] = {
  { "ehlo", smtp_ehlo, 0 }
, { "helo", smtp_helo, 0 }
, { "auth", smtp_auth, flush }
, { "help", smtp_help, 0 }
, { "quit", smtp_quit, 0 }
, { 0, err_authoriz, 0 }
} ;

void main(argc,argv)
int argc;
char **argv;
{
  sig_alarmcatch(die);
  sig_pipeignore();
 
  hostname = argv[1];
  if (!hostname) die_usage();
  childargs = argv + 2;
  if (!*childargs) die_usage();
 
  smtp_greet();
  commands(&ssin,smtpcommands);
  die();
}

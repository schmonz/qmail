#include "auto_qmail.h"
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
#include "control.h"
#include "error.h"

int timeout = 1200;

void die() { _exit(1); }
void die_noretry() { _exit(17); }

int safewrite(fd,buf,len) int fd; char *buf; int len;
{
  int r;
  r = timeoutwrite(timeout,fd,buf,len);
  if (r <= 0) die();
  return r;
}

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(safewrite,1,ssoutbuf,sizeof ssoutbuf);

void puts(char *s) { substdio_puts(&ssout,s); }
void flush() { substdio_flush(&ssout); }

void pop3_err(char *s) {
  puts("-ERR ");
  puts(s);
  puts("\r\n");
  flush();
}

void smtp_err(char *s) {
  puts(s);
  puts("\r\n");
  flush();
}

struct authup_error {
  char *name;
  char *message;
  char *smtpcode;
  char *smtperror;
  void (*die)();
};

struct authup_error e[] = {
  { "control", "unable to read controls",      "421", "4.3.0", die         }
, { "nomem",   "out of memory",                "451", "4.3.0", die         }
, { "alarm",   "timeout",                      "451", "4.4.2", die_noretry }
, { "pipe",    "unable to open pipe",          "454", "4.3.0", die         }
, { "write",   "unable to write pipe",         "454", "4.3.0", die         }
, { "fork",    "unable to fork",               "454", "4.3.0", die         }
, { "child",   "aack, child crashed",          "454", "4.3.0", die         }
, { "badauth", "authorization failed",         "535", "5.7.0", die         }
, { "noauth",  "auth type unimplemented",      "504", "5.5.1", die         }
, { "input",   "malformed auth input",         "501", "5.5.4", die         }
, { "authabrt","auth exchange cancelled",      "501", "5.0.0", die         }
, { 0,         "unknown or unspecified error", "421", "4.3.0", die         }
};

void pop3_auth_error(struct authup_error ae) {
    puts("-ERR");
    puts(" qmail-authup ");
    puts(ae.message);
}

void smtp_auth_error(struct authup_error ae) {
    puts(ae.smtpcode);
    puts(" qmail-authup ");
    puts(ae.message);
    puts(" (#");
    puts(ae.smtperror);
    puts(")");
}

void (*protocol_error)();

void authup_die(const char *name) {
  int i;
  for (i = 0;e[i].name;++i) if (case_equals(e[i].name,name)) break;
  protocol_error(e[i]);
  puts("\r\n");
  flush();
  e[i].die();
}

void die_usage() { puts("usage: qmail-authup <pop3|smtp> subprogram\n"); flush(); die(); }

void pop3_err_authoriz() { pop3_err("qmail-authup authorization first"); }
void smtp_err_authoriz() { smtp_err("530 qmail-authup authentication required (#5.7.1)"); }

void pop3_err_syntax() { pop3_err("qmail-authup syntax error"); }
void pop3_err_wantuser() { pop3_err("qmail-authup USER first"); }

int saferead(fd,buf,len) int fd; char *buf; int len;
{
  int r;
  r = timeoutread(timeout,fd,buf,len);
  if (r == -1) if (errno == error_timeout) authup_die("alarm");
  if (r <= 0) die();
  return r;
}

char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(saferead,0,ssinbuf,sizeof ssinbuf);

stralloc hostname = {0};
stralloc username = {0};
char **childargs;
substdio ssup;
char upbuf[128];

void pop3_okay(arg) char *arg; { puts("+OK \r\n"); flush(); }
void pop3_quit(arg) char *arg; { pop3_okay(0); _exit(0); }
void smtp_quit() { puts("221 "); smtp_err(hostname.s); _exit(0); }

/* XXX POP3 only */
char unique[FMT_ULONG + FMT_ULONG + 3];
int seenuser = 0;

// XXX merge these two doanddie()s
void pop3_doanddie(user,userlen,pass)
char *user;
unsigned int userlen; /* including 0 byte */
char *pass;
{
  int child;
  int wstat;
  int pi[2];
 
  close(3);
  if (pipe(pi) == -1) authup_die("pipe");
  if (pi[0] != 3) authup_die("pipe");
  switch(child = fork()) {
    case -1:
      authup_die("fork");
    case 0:
      close(pi[1]);
      sig_pipedefault();
      execvp(*childargs,childargs);
      _exit(1);
  }
  close(pi[0]);
  substdio_fdbuf(&ssup,write,pi[1],upbuf,sizeof upbuf);
  if (substdio_put(&ssup,user,userlen) == -1) authup_die("write");
  if (substdio_put(&ssup,pass,str_len(pass) + 1) == -1) authup_die("write");
  if (substdio_puts(&ssup,"<") == -1) authup_die("write");
  if (substdio_puts(&ssup,unique) == -1) authup_die("write");
  if (substdio_puts(&ssup,hostname.s) == -1) authup_die("write");
  if (substdio_put(&ssup,">",2) == -1) authup_die("write");
  if (substdio_flush(&ssup) == -1) authup_die("write");
  close(pi[1]);
  byte_zero(pass,str_len(pass));
  byte_zero(upbuf,sizeof upbuf);
  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) authup_die("child");
  if (wait_exitcode(wstat)) authup_die("badauth");
  die();
}

void pop3_greet()
{
  char *s;
  s = unique;
  s += fmt_uint(s,getpid());
  *s++ = '.';
  s += fmt_ulong(s,(unsigned long) now());
  *s++ = '@';
  *s++ = 0;
  puts("+OK <");
  puts(unique);
  puts(hostname.s);
  puts(">\r\n");
  flush();
}

void pop3_user(arg) char *arg;
{
  if (!*arg) { pop3_err_syntax(); return; }
  pop3_okay(0);
  seenuser = 1;
  if (!stralloc_copys(&username,arg)) authup_die("nomem");
  if (!stralloc_0(&username)) authup_die("nomem");
}

void pop3_pass(arg) char *arg;
{
  if (!seenuser) { pop3_err_wantuser(); return; }
  if (!*arg) { pop3_err_syntax(); return; }
  pop3_doanddie(username.s,username.len,arg);
}

void pop3_apop(arg) char *arg;
{
  char *space;
  space = arg + str_chr(arg,' ');
  if (!*space) { pop3_err_syntax(); return; }
  *space++ = 0;
  pop3_doanddie(arg,space - arg,space);
}

/* XXX SMTP only */
static stralloc authin = {0};
static stralloc user = {0};
static stralloc pass = {0};
static stralloc resp = {0};

void pop3_putenv(void) {
  if (!env_put2("POP3USER",user.s)) authup_die("nomem");
}

void smtp_putenv(void) {
  if (!env_put2("SMTPUSER",user.s)) authup_die("nomem");
}

int is_checkpassword_failure(int exitcode) {
  return (exitcode == 1 || exitcode == 2 || exitcode == 111);
}

/* doanddie(char *user, unsigned int userlen, char *pass) */
void smtp_doanddie(void) {
  int child;
  int wstat;
  int pi[2];
 
  /* if (fd_copy(2,1) == -1) die_pipe() */
  close(3);
  if (pipe(pi) == -1) authup_die("pipe");
  if (pi[0] != 3) authup_die("pipe");
  switch(child = fork()) {
    case -1:
      authup_die("fork");
    case 0:
      close(pi[1]);
      sig_pipedefault();
      smtp_putenv();
      execvp(*childargs,childargs);
      _exit(1);
  }
  close(pi[0]);
  substdio_fdbuf(&ssup,write,pi[1],upbuf,sizeof upbuf);
  /* if (substdio_put(&ssup,user,userlen) == -1) die_write() */
  if (substdio_put(&ssup,user.s,user.len) == -1) authup_die("write");
  /* if (substdio_put(&ssup,pass,str_len(pass) + 1) == -1) die_write() */
  if (substdio_put(&ssup,pass.s,pass.len) == -1) authup_die("write");
  /* unique/hostname thing */
  if (substdio_flush(&ssup) == -1) authup_die("write");
  close(pi[1]);
  /* byte_zero(pass,str_len(pass)); */
  byte_zero(upbuf,sizeof upbuf);
  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) authup_die("child");
  if (is_checkpassword_failure(wait_exitcode(wstat))) authup_die("badauth");
  die_noretry();
}

void smtp_greet()
{
  puts("220 ");
  puts(hostname.s);
  puts(" ESMTP\r\n");
  flush();
}

void smtp_helo(arg) char *arg;
{
  puts("250 ");
  smtp_err(hostname.s);
}

void smtp_ehlo(arg) char *arg;
{
  puts("250-");
  puts(hostname.s);
  puts("\r\n250-AUTH LOGIN PLAIN");
  puts("\r\n250-AUTH=LOGIN PLAIN");
  smtp_err("\r\n250-PIPELINING\r\n250 8BITMIME");
}

void smtp_authgetl(void)
{
  int i;

  if (!stralloc_copys(&authin,"")) authup_die("nomem");
  for (;;) {
    if (!stralloc_readyplus(&authin,1)) authup_die("nomem"); /* XXX */
    i = substdio_get(&ssin,authin.s + authin.len,1);
    if (i != 1) die();
    if (authin.s[authin.len] == '\n') break;
    ++authin.len;
  }

  if (authin.len > 0) if (authin.s[authin.len - 1] == '\r') --authin.len;
  authin.s[authin.len] = 0;
  if (*authin.s == '*' && *(authin.s + 1) == 0) authup_die("authabrt");
  if (authin.len == 0) authup_die("input");
}

void auth_login(arg) char *arg;
{
  int r;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&user)) == 1) authup_die("input");
  }
  else {
    smtp_err("334 VXNlcm5hbWU6"); /* Username: */
    smtp_authgetl();
    if ((r = b64decode(authin.s,authin.len,&user)) == 1) authup_die("input");
  }
  if (r == -1) authup_die("nomem");

  smtp_err("334 UGFzc3dvcmQ6"); /* Password: */

  smtp_authgetl();
  if ((r = b64decode(authin.s,authin.len,&pass)) == 1) authup_die("input");
  if (r == -1) authup_die("nomem");

  if (!user.len || !pass.len) authup_die("input");
  if (!stralloc_0(&user)) authup_die("nomem");
  if (!stralloc_0(&pass)) authup_die("nomem");
  smtp_doanddie();
}

void auth_plain(arg) char *arg;
{
  int r, id = 0;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&resp)) == 1) authup_die("input");
  }
  else {
    smtp_err("334 ");
    smtp_authgetl();
    if ((r = b64decode(authin.s,authin.len,&resp)) == 1) authup_die("input");
  }
  if (r == -1 || !stralloc_0(&resp)) authup_die("nomem");
  while (resp.s[id]) id++; /* ignore authorize-id */

  if (resp.len > id + 1)
    if (!stralloc_copys(&user,resp.s + id + 1)) authup_die("nomem");
  if (resp.len > id + user.len + 2)
    if (!stralloc_copys(&pass,resp.s + id + user.len + 2)) authup_die("nomem");

  if (!user.len || !pass.len) authup_die("input");
  if (!stralloc_0(&user)) authup_die("nomem");
  if (!stralloc_0(&pass)) authup_die("nomem");
  smtp_doanddie();
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
  authup_die("noauth");
}

void smtp_help() { smtp_err("214 qmail-authup home page: https://schmonz.com/qmail/authutils"); }

struct commands pop3commands[] = {
  { "user", pop3_user, 0 }
, { "pass", pop3_pass, 0 }
, { "apop", pop3_apop, 0 }
, { "quit", pop3_quit, 0 }
, { "noop", pop3_okay, 0 }
, { 0, pop3_err_authoriz, 0 }
};

struct commands smtpcommands[] = {
  { "ehlo", smtp_ehlo, 0 }
, { "helo", smtp_helo, 0 }
, { "auth", smtp_auth, flush }
, { "help", smtp_help, 0 }
, { "quit", smtp_quit, 0 }
, { 0, smtp_err_authoriz, 0 }
};

int should_greet() {
  char *x;
  int r;

  x = env_get("REUP");
  if (!x) return 1;
  if (!scan_ulong(x,&r)) return 1;
  if (r > 1) return 0;
  return 1;
}

int control_readgreeting(char *p) {
  stralloc file = {0};
  int retval;

  if (!stralloc_copys(&file,"control/")) authup_die("nomem");
  if (!stralloc_cats(&file,p)) authup_die("nomem");
  if (!stralloc_cats(&file,"greeting")) authup_die("nomem");
  if (!stralloc_0(&file)) authup_die("nomem");

  retval = control_rldef(&hostname,file.s,1,(char *) 0);
  if (retval != 1) retval = -1;

  if (!stralloc_0(&hostname)) authup_die("nomem");

  return retval;
}

int control_readtimeout(char *p) {
  stralloc file = {0};

  if (!stralloc_copys(&file,"control/timeout")) authup_die("nomem");
  if (!stralloc_cats(&file,p)) authup_die("nomem");
  if (!stralloc_cats(&file,"d")) authup_die("nomem");
  if (!stralloc_0(&file)) authup_die("nomem");

  return control_readint(&timeout,file.s);
}

void doprotocol(char *p, void *errorfn, void *greetfn, struct commands *c) {
  protocol_error = errorfn;
  void (*greetingfn)() = greetfn;

  if (chdir(auto_qmail) == -1) authup_die("control");
  if (control_init() == -1) authup_die("control");
  if (control_readgreeting(p) == -1) authup_die("control");
  if (control_readtimeout(p) == -1) authup_die("control");
  if (should_greet()) greetingfn();
  commands(&ssin,c);

  die();
}

int main(int argc,char **argv) {
  char *protocol;

  sig_alarmcatch(die);
  sig_pipeignore();
 
  protocol = argv[1];
  if (!protocol) die_usage();

  childargs = argv + 2;
  if (!*childargs) die_usage();

  if (case_equals("pop3",protocol))
    doprotocol("pop3",pop3_auth_error,pop3_greet,pop3commands);
  if (case_equals("smtp",protocol))
    doprotocol("smtp",smtp_auth_error,smtp_greet,smtpcommands);
  die_usage();
}

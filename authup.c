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
#include "scan.h"
#include "exit.h"
#include "readwrite.h"
#include "timeoutread.h"
#include "timeoutwrite.h"
#include "acceptutils_base64.h"
#include "case.h"
#include "env.h"
#include "control.h"
#include "error.h"
#include "open.h"
#include "acceptutils_ucspitls.h"

#define HOMEPAGE "https://schmonz.com/qmail/acceptutils"
#define PROGNAME "authup"

#define EXITCODE_CHECKPASSWORD_UNACCEPTABLE   1
#define EXITCODE_CHECKPASSWORD_MISUSED        2
#define EXITCODE_CHECKPASSWORD_TEMPFAIL     111
/* sync with fixsmtpio.h */
#define EXITCODE_FIXSMTPIO_TIMEOUT           16
#define EXITCODE_FIXSMTPIO_PARSEFAIL         18

static int timeout = 1200;
int starttls = 0;
int seentls = 0;

void die()         { _exit( 1); }
void die_noretry() { _exit(12); }

int safewrite(int fd,char *buf,int len) {
  int r;
  r = timeoutwrite(timeout,fd,buf,len);
  if (r <= 0) die();
  return r;
}

char ssoutbuf[SUBSTDIO_OUTSIZE];
substdio ssout = SUBSTDIO_FDBUF(safewrite,1,ssoutbuf,sizeof ssoutbuf);

void puts(char *s) { substdio_puts(&ssout,s); }
void flush() { substdio_flush(&ssout); }

void pop3_err(char *s) { puts("-ERR "); puts(s); puts("\r\n"); flush(); }
void smtp_out(char *s) {                puts(s); puts("\r\n"); flush(); }

struct authup_error {
  char *name;
  char *message;
  char *smtpcode;
  char *smtperror;
  int sleep;
  void (*die)();
};

struct authup_error e[] = {
  { "control", "unable to read controls",      "421", "4.3.0", 0, die_noretry }
, { "nomem",   "out of memory",                "451", "4.3.0", 0, die_noretry }
, { "alarm",   "timeout",                      "451", "4.4.2", 0, die_noretry }
, { "pipe",    "unable to open pipe",          "454", "4.3.0", 0, die_noretry }
, { "read",    "unable to read pipe",          "454", "4.3.0", 0, die_noretry }
, { "write",   "unable to write pipe",         "454", "4.3.0", 0, die_noretry }
, { "fork",    "unable to fork",               "454", "4.3.0", 0, die_noretry }
, { "wait",    "unable to wait for child",     "454", "4.3.0", 0, die_noretry }
, { "crash",   "aack, child crashed",          "454", "4.3.0", 0, die_noretry }
, { "badauth", "authorization failed",         "535", "5.7.0", 5, die         }
, { "noauth",  "auth type unimplemented",      "504", "5.5.1", 5, die         }
, { "input",   "malformed auth input",         "501", "5.5.4", 5, die         }
, { "authabrt","auth exchange cancelled",      "501", "5.0.0", 5, die         }
, { "protocol","protocol exchange ended",      "501", "5.0.0", 0, die_noretry }
, { "starttls","TLS temporarily not available","454", "5.7.3", 0, die         }
, { "needtls", "TLS required",                 "535", "5.7.1", 0, die         }
, { 0,         "unknown or unspecified error", "421", "4.3.0", 0, die_noretry }
};

void pop3_auth_error(struct authup_error ae) {
  puts("-ERR");
  puts(" " PROGNAME " ");
  puts(ae.message);
}

void smtp_auth_error(struct authup_error ae) {
  puts(ae.smtpcode);
  puts(" " PROGNAME " ");
  puts(ae.message);
  puts(" (#");
  puts(ae.smtperror);
  puts(")");
}

void (*protocol_error)();

void pop3_sleep(int s) { return; }
void smtp_sleep(int s) { sleep(s); }
void (*protocol_sleep)();

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
}

void authup_die(const char *name) {
  int i;
  for (i = 0;e[i].name;++i) if (case_equals(e[i].name,name)) break;
  protocol_sleep(e[i].sleep);
  protocol_error(e[i]);
  puts("\r\n");
  flush();
  e[i].die();
}

void die_nomem() { authup_die("nomem"); }

void die_usage() { errflush("usage: " PROGNAME " <smtp|pop3> prog"); die(); }

void smtp_err_authoriz() { smtp_out("530 " PROGNAME " authentication required (#5.7.1)"); }
void pop3_err_authoriz() { pop3_err(PROGNAME " authorization first"); }

void pop3_err_syntax()   { pop3_err(PROGNAME " syntax error"); }
void pop3_err_wantuser() { pop3_err(PROGNAME " USER first"); }

int saferead(int fd,char *buf,int len) {
  int r;
  r = timeoutread(timeout,fd,buf,len);
  if (r == -1) if (errno == error_timeout) authup_die("alarm");
  if (r <= 0) authup_die("read");
  return r;
}

char ssinbuf[SUBSTDIO_INSIZE];
substdio ssin = SUBSTDIO_FDBUF(saferead,0,ssinbuf,sizeof ssinbuf);

stralloc greeting = {0};
stralloc capabilities = {0};
char **childargs;

void pop3_okay() { puts("+OK \r\n"); flush(); }
void pop3_quit() { pop3_okay(); _exit(0); }
void smtp_quit() { puts("221 "); smtp_out(greeting.s); _exit(0); }

stralloc username = {0};
stralloc password = {0};
stralloc timestamp = {0};

void exit_according_to_child_exit(int exitcode) {
  switch (exitcode) {
    case EXITCODE_CHECKPASSWORD_UNACCEPTABLE:
    case EXITCODE_CHECKPASSWORD_MISUSED:
    case EXITCODE_CHECKPASSWORD_TEMPFAIL:
      authup_die("badauth");
    case EXITCODE_FIXSMTPIO_TIMEOUT:
      authup_die("alarm");
    case EXITCODE_FIXSMTPIO_PARSEFAIL:
      authup_die("control");
    default:
      _exit(0);
  }
}

void logtry(char *username) {
  substdio_puts(&sserr,PROGNAME ": login attempt by ");
  substdio_puts(&sserr,username);
  substdio_putsflush(&sserr,"\n");
}

void checkpassword(stralloc *username,stralloc *password,stralloc *timestamp) {
  int child;
  int wstat;
  int pi[2];
  char upbuf[SUBSTDIO_OUTSIZE];
  substdio ssup;

  close(3);
  if (pipe(pi) == -1) authup_die("pipe");
  if (pi[0] != 3) authup_die("pipe");
  switch((child = fork())) {
    case -1:
      authup_die("fork");
    case 0:
      close(pi[1]);
      sig_pipedefault();
      if (!stralloc_0(username)) authup_die("nomem");
      logtry(username->s);
      if (!env_put2("AUTHUP_USER",username->s)) authup_die("nomem");
      execvp(*childargs,childargs);
      authup_die("fork");
  }
  close(pi[0]);
  substdio_fdbuf(&ssup,write,pi[1],upbuf,sizeof upbuf);

  if (!stralloc_0(username)) authup_die("nomem");
  if (substdio_put(&ssup,username->s,username->len) == -1) authup_die("write");
  byte_zero(username->s,username->len);

  if (!stralloc_0(password)) authup_die("nomem");
  if (substdio_put(&ssup,password->s,password->len) == -1) authup_die("write");
  byte_zero(password->s,password->len);

  if (!stralloc_0(timestamp)) authup_die("nomem");
  if (substdio_put(&ssup,timestamp->s,timestamp->len) == -1) authup_die("write");
  byte_zero(timestamp->s,timestamp->len);

  if (substdio_flush(&ssup) == -1) authup_die("write");
  close(pi[1]);
  byte_zero(upbuf,sizeof upbuf);

  if (wait_pid(&wstat,child) == -1) authup_die("wait");
  if (wait_crashed(wstat)) authup_die("crash");

  exit_according_to_child_exit(wait_exitcode(wstat));
}

static char unique[FMT_ULONG + FMT_ULONG + 3];

void pop3_greet() {
  char *s;
  s = unique;
  s += fmt_uint(s,getpid());
  *s++ = '.';
  s += fmt_ulong(s,(unsigned long) now());
  *s++ = '@';
  *s++ = 0;
  puts("+OK <");
  puts(unique);
  puts(greeting.s);
  puts(">\r\n");
  flush();
}

void pop3_format_capa(stralloc *multiline) {
  if (!stralloc_cats(multiline,".\r\n")) authup_die("nomem");
}

void pop3_capa(char *arg) {
  puts("+OK capability list follows\r\n");
  if (starttls && !seentls) puts("STLS\r\n");
  puts("USER\r\n");
  puts(capabilities.s);
  flush();
}

static int seenuser = 0;

void pop3_stls(char *arg) {
  if (!starttls || seentls) return pop3_err("STLS not available");
  puts("+OK starting TLS negotiation\r\n");
  flush();

  if (!starttls_init() || !starttls_info(die_nomem)) authup_die("starttls");
  /* reset state */
  seenuser = 0;

  seentls = 1;
}

void pop3_user(char *arg) {
  if (starttls && !seentls) authup_die("needtls");
  if (!*arg) { pop3_err_syntax(); return; }
  pop3_okay();
  seenuser = 1;
  if (!stralloc_copys(&username,arg)) authup_die("nomem");
}

void pop3_pass(char *arg) {
  if (!seenuser) { pop3_err_wantuser(); return; }
  if (!*arg) { pop3_err_syntax(); return; }

  if (!stralloc_copys(&password,arg)) authup_die("nomem");
  byte_zero(arg,str_len(arg));

  if (!stralloc_copys(&timestamp,"<")) authup_die("nomem");
  if (!stralloc_cats(&timestamp,unique)) authup_die("nomem");
  if (!stralloc_cats(&timestamp,greeting.s)) authup_die("nomem");
  if (!stralloc_cats(&timestamp,">")) authup_die("nomem");

  checkpassword(&username,&password,&timestamp);
}

void smtp_greet() {
  puts("220 ");
  puts(greeting.s);
  puts(" ESMTP\r\n");
  flush();
}

void smtp_helo(char *arg) {
  puts("250 ");
  smtp_out(greeting.s);
}

// copy from fixsmtpio_munge.c:change_last_line_fourth_char_to_space()
void smtp_format_ehlo(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = multiline->len - 2; i >= 0; i--) {
    if (multiline->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  capabilities.s[pos+3] = ' ';
}

void smtp_ehlo(char *arg) {
  char *x;
  puts("250-"); puts(greeting.s); puts("\r\n");
  if (starttls && !seentls) puts("250-STARTTLS\r\n");
  puts("250-AUTH LOGIN PLAIN\r\n");
  if ((x = env_get("AUTHUP_SASL_BROKEN_CLIENTS")))
    puts("250-AUTH=LOGIN PLAIN\r\n");
  puts(capabilities.s);
  flush();
}

void smtp_starttls() {
  if (!starttls || seentls) return smtp_out("502 unimplemented (#5.5.1)");
  smtp_out("220 Ready to start TLS (#5.7.0)");

  if (!starttls_init() || !starttls_info(die_nomem)) authup_die("starttls");
  /* reset state */
  ssin.p = 0;

  seentls = 1;
}

static stralloc authin = {0};

void smtp_authgetl() {
  int i;

  if (!stralloc_copys(&authin,"")) authup_die("nomem");

  for (;;) {
    if (!stralloc_readyplus(&authin,1)) authup_die("nomem"); /* XXX */
    i = substdio_get(&ssin,authin.s + authin.len,1);
    if (i != 1) authup_die("read");
    if (authin.s[authin.len] == '\n') break;
    ++authin.len;
  }

  if (authin.len > 0) if (authin.s[authin.len - 1] == '\r') --authin.len;
  authin.s[authin.len] = 0;

  if (*authin.s == '*' && *(authin.s + 1) == 0) authup_die("authabrt");
  if (authin.len == 0) authup_die("input");
}

void auth_login(char *arg) {
  int r;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&username)) == 1) authup_die("input");
  }
  else {
    smtp_out("334 VXNlcm5hbWU6"); /* Username: */
    smtp_authgetl();
    if ((r = b64decode(authin.s,authin.len,&username)) == 1) authup_die("input");
  }
  if (r == -1) authup_die("nomem");

  smtp_out("334 UGFzc3dvcmQ6"); /* Password: */

  smtp_authgetl();
  if ((r = b64decode(authin.s,authin.len,&password)) == 1) authup_die("input");
  if (r == -1) authup_die("nomem");

  if (!username.len || !password.len) authup_die("input");
  checkpassword(&username,&password,&timestamp);
}

static stralloc resp = {0};

void auth_plain(char *arg) {
  int r, id = 0;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&resp)) == 1) authup_die("input");
  }
  else {
    smtp_out("334 ");
    smtp_authgetl();
    if ((r = b64decode(authin.s,authin.len,&resp)) == 1) authup_die("input");
  }
  if (r == -1 || !stralloc_0(&resp)) authup_die("nomem");
  while (resp.s[id]) id++; /* ignore authorize-id */

  if (resp.len > id + 1)
    if (!stralloc_copys(&username,resp.s + id + 1)) authup_die("nomem");
  if (resp.len > id + username.len + 2)
    if (!stralloc_copys(&password,resp.s + id + username.len + 2)) authup_die("nomem");

  if (!username.len || !password.len) authup_die("input");
  checkpassword(&username,&password,&timestamp);
}

void smtp_auth(char *arg) {
  int i;
  char *cmd = arg;

  if (starttls && !seentls) authup_die("needtls");

  i = str_chr(cmd,' ');
  arg = cmd + i;
  while (*arg == ' ') ++arg;
  cmd[i] = 0;

  if (case_equals("login",cmd)) auth_login(arg);
  if (case_equals("plain",cmd)) auth_plain(arg);
  authup_die("noauth");
}

void smtp_help() {
  smtp_out("214 " PROGNAME " home page: " HOMEPAGE);
}

void smtp_noop() {
  smtp_out("250 ok");
}

struct commands pop3commands[] = {
  { "stls", pop3_stls, 0 }
, { "user", pop3_user, 0 }
, { "pass", pop3_pass, 0 }
, { "quit", pop3_quit, 0 }
, { "capa", pop3_capa, 0 }
, { "noop", pop3_okay, 0 }
, { 0, pop3_err_authoriz, 0 }
};

struct commands smtpcommands[] = {
  { "starttls", smtp_starttls, 0 }
, { "auth", smtp_auth, flush }
, { "quit", smtp_quit, 0 }
, { "helo", smtp_helo, 0 }
, { "ehlo", smtp_ehlo, 0 }
, { "help", smtp_help, 0 }
, { "noop", smtp_noop, 0 }
, { 0, smtp_err_authoriz, 0 }
};

struct protocol {
  char *name;
  char *cap_prefix;
  void (*cap_format_response)();
  void (*error)();
  void (*sleep)();
  void (*greet)();
  struct commands *c;
};

struct protocol p[] = {
  { "pop3", "",     pop3_format_capa, pop3_auth_error, pop3_sleep, pop3_greet, pop3commands }
, { "smtp", "250-", smtp_format_ehlo, smtp_auth_error, smtp_sleep, smtp_greet, smtpcommands }
, { 0,      "",     0,                die_usage,       0,     die_usage,  0            }
};

int control_readgreeting(char *p) {
  stralloc file = {0};
  int retval;

  if (!stralloc_copys(&file,"control/")) authup_die("nomem");
  if (!stralloc_cats(&file,p)) authup_die("nomem");
  if (!stralloc_cats(&file,"greeting")) authup_die("nomem");
  if (!stralloc_0(&file)) authup_die("nomem");

  retval = control_rldef(&greeting,file.s,1,(char *) 0);
  if (retval != 1) retval = -1;

  if (!stralloc_0(&greeting)) authup_die("nomem");

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

int control_readcapabilities(struct protocol p) {
  stralloc file = {0};
  stralloc lines = {0};
  int linestart;
  int pos;

  if (!stralloc_copys(&file,"control/")) authup_die("nomem");
  if (!stralloc_cats(&file,p.name)) authup_die("nomem");
  if (!stralloc_cats(&file,"capabilities")) authup_die("nomem");
  if (!stralloc_0(&file)) authup_die("nomem");

  if (control_readfile(&lines,file.s,0) != 1) return -1;

  if (!stralloc_copys(&capabilities,"")) authup_die("nomem");
  for (linestart = 0, pos = 0; pos < lines.len; pos++) {
    if (lines.s[pos] == '\0') {
      if (!stralloc_cats(&capabilities,p.cap_prefix)) authup_die("nomem");
      if (!stralloc_cats(&capabilities,lines.s+linestart)) authup_die("nomem");
      if (!stralloc_cats(&capabilities,"\r\n")) authup_die("nomem");
      linestart = pos + 1;
    }
  }
  p.cap_format_response(&capabilities);
  if (!stralloc_0(&capabilities)) authup_die("nomem");

  return 1;
}

int should_greet() {
  char *x;
  int r;

  if (!(x = env_get("REUP"))) return 1;
  if (!scan_ulong(x,&r)) return 1;
  if (r > 1) return 0;
  return 1;
}

void doprotocol(struct protocol p) {
  protocol_error = p.error;
  protocol_sleep = p.sleep;

  if (chdir(auto_qmail) == -1) authup_die("control");
  if (control_init() == -1) authup_die("control");
  if (control_readgreeting(p.name) == -1) authup_die("control");
  if (control_readtimeout(p.name) == -1) authup_die("control");
  if (control_readcapabilities(p) == -1) authup_die("control");
  if (should_greet()) p.greet();
  commands(&ssin,p.c);
  authup_die("protocol");
}

int main(int argc,char **argv) {
  char *protocol;
  int i;

  sig_alarmcatch(die);
  sig_pipeignore();

  protocol = argv[1];
  if (!protocol) die_usage();

  childargs = argv + 2;
  if (!*childargs) die_usage();

  starttls = ucspitls_level();

  for (i = 0; p[i].name; ++i)
    if (case_equals(p[i].name,protocol))
      doprotocol(p[i]);
  die_usage();
}

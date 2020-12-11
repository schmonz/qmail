#include "auto_qmail.h"
#include "commands.h"
#include "sig.h"
#include "substdio.h"
#include "wait.h"
#include "str.h"
#include "byte.h"
#include "now.h"
#include "fmt.h"
#include "scan.h"
#include "readwrite.h"
#include "timeoutread.h"
#include "timeoutwrite.h"
#include "case.h"
#include "env.h"
#include "control.h"
#include "error.h"
#include "sgetopt.h"

#include "acceptutils_base64.h"
#include "acceptutils_pfilter.h"
#include "acceptutils_stralloc.h"
#include "acceptutils_ucspitls.h"
#include "acceptutils_unistd.h"

#define HOMEPAGE "https://schmonz.com/qmail/acceptutils"
#define PROGNAME "authup"

#define EXITCODE_CHECKPASSWORD_UNACCEPTABLE   1
#define EXITCODE_CHECKPASSWORD_MISUSED        2
#define EXITCODE_CHECKPASSWORD_TEMPFAIL     111
/* sync with fixsmtpio.h */
#define EXITCODE_FIXSMTPIO_TIMEOUT           16
#define EXITCODE_FIXSMTPIO_PARSEFAIL         18

static int auth_tries_remaining = 0;

static int timeout = 1200;
static int tls_level = UCSPITLS_UNAVAILABLE;
static int in_tls = 0;

static void die()         { unistd_exit( 1); }

static int safewrite(int fd,char *buf,int len) {
  int r;
  r = timeoutwrite(timeout,fd,buf,len);
  if (r <= 0) die();
  return r;
}

static char ssoutbuf[SUBSTDIO_OUTSIZE];
static substdio ssout = SUBSTDIO_FDBUF(safewrite,1,ssoutbuf,sizeof ssoutbuf);

static void out(const char *s) { substdio_puts(&ssout,s); }
static void flush() { substdio_flush(&ssout); }

static void pop3_err(const char *s) { out("-ERR "); out(s); out("\r\n"); flush(); }
static void smtp_out(const char *s) {               out(s); out("\r\n"); flush(); }

struct authup_error {
  const char *name;
  const char *message;
  const char *smtpcode;
  const char *smtperror;
};

static const struct authup_error fatals[] = {
  { "control", "unable to read controls",      "421", "4.3.0" }
, { "nomem",   "out of memory",                "451", "4.3.0" }
, { "alarm",   "timeout",                      "451", "4.4.2" }
, { "pipe",    "unable to open pipe",          "454", "4.3.0" }
, { "read",    "unable to read pipe",          "454", "4.3.0" }
, { "write",   "unable to write pipe",         "454", "4.3.0" }
, { "fork",    "unable to fork",               "454", "4.3.0" }
, { "wait",    "unable to wait for child",     "454", "4.3.0" }
, { "crash",   "aack, child crashed",          "454", "4.3.0" }
, { "badauth", "authorization failed",         "535", "5.7.0" }
, { "protocol","protocol exchange ended",      "501", "5.0.0" }
, { 0,         "unknown or unspecified error", "421", "4.3.0" }
};

static const struct authup_error errors[] = {
  { "badauth", "authorization failed",         "535", "5.7.0" }
, { "noauth",  "auth type unimplemented",      "504", "5.5.1" }
, { "input",   "malformed auth input",         "501", "5.5.4" }
, { "authabrt","auth exchange cancelled",      "501", "5.0.0" }
, { "starttls","TLS temporarily not available","454", "5.7.3" }
, { "needtls", "Must start TLS first",         "530", "5.7.0" }
};

static void pop3_auth_error(struct authup_error ae) {
  out("-ERR " PROGNAME " ");
  out(ae.message);
}

static void smtp_auth_error(struct authup_error ae) {
  out(ae.smtpcode);
  out(" " PROGNAME " ");
  out(ae.message);
  out(" (#");
  out(ae.smtperror);
  out(")");
}

static void (*protocol_error)();

static char sserrbuf[SUBSTDIO_OUTSIZE];
static substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

static void authup_die(const char *name) {
  int i;
  for (i = 0;fatals[i].name;++i) if (case_equals(fatals[i].name,name)) break;
  protocol_error(fatals[i]);
  out("\r\n");
  flush();
  die();
}

static void authup_err(const char *name) {
  int i;
  for (i = 0;errors[i].name;++i) if (case_equals(errors[i].name,name)) break;
  protocol_error(errors[i]);
  out("\r\n");
  flush();
}

static void die_nomem(const char *caller,const char *alloc_fn) {
  substdio_puts(&sserr,PROGNAME ": die_nomem: ");
  if (caller) {
    substdio_puts(&sserr,caller);
    substdio_puts(&sserr,": ");
  }
  if (alloc_fn) {
    substdio_puts(&sserr,alloc_fn);
  }
  substdio_putsflush(&sserr,"\n");
  authup_die("nomem");
}

static void die_usage() {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,"usage: " PROGNAME " [ -t tries ] <smtp|pop3> prog");
  substdio_putsflush(&sserr,"\n");
  die();
}

static void smtp_err_authoriz() { smtp_out("530 " PROGNAME " authentication required (#5.7.1)"); }
static void pop3_err_authoriz() { pop3_err(PROGNAME " authorization first"); }

static void pop3_err_syntax()   { pop3_err(PROGNAME " syntax error"); }
static void pop3_err_wantuser() { pop3_err(PROGNAME " USER first"); }

static int saferead(int fd,char *buf,int len) {
  int r;
  r = timeoutread(timeout,fd,buf,len);
  if (r == -1) if (errno == error_timeout) authup_die("alarm");
  if (r <= 0) authup_die("read");
  return r;
}

static char ssinbuf[SUBSTDIO_INSIZE];
static substdio ssin = SUBSTDIO_FDBUF(saferead,0,ssinbuf,sizeof ssinbuf);

static stralloc greeting = {0};
static stralloc capabilities = {0};
static char **childargs;

static void pop3_okay() { out("+OK \r\n"); flush(); }
static void pop3_quit() { pop3_okay(); unistd_exit(0); }
static void smtp_quit() { out("221 "); smtp_out(greeting.s); unistd_exit(0); }

static stralloc username = {0};
static stralloc logname = {0};
static stralloc password = {0};
static stralloc timestamp = {0};

static char *format_pid(unsigned int pid) {
  char pidbuf[FMT_ULONG];
  stralloc sa = {0};
  if (!sa.len) {
    int len = fmt_ulong(pidbuf,pid);
    if (len) copyb(&sa,pidbuf,len);
    append0(&sa);
  }
  return sa.s;
}

static char *authup_pid;

static void logpass(int checkpassword_pid) {
  substdio_puts(&sserr,PROGNAME);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,authup_pid);
  substdio_puts(&sserr," checkpassword ");
  substdio_puts(&sserr,format_pid(checkpassword_pid));
  substdio_puts(&sserr," succeeded, child completed");
  substdio_putsflush(&sserr,"\n");
}

static void logfail(int checkpassword_pid) {
  substdio_puts(&sserr,PROGNAME);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,authup_pid);
  substdio_puts(&sserr," checkpassword ");
  substdio_puts(&sserr,format_pid(checkpassword_pid));
  substdio_puts(&sserr," failed, ");
  substdio_puts(&sserr,format_pid(auth_tries_remaining));
  substdio_puts(&sserr," remaining");
  substdio_putsflush(&sserr,"\n");
}

static void logstart(char *protocol) {
  substdio_puts(&sserr,PROGNAME);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,authup_pid);
  substdio_puts(&sserr," protocol ");
  substdio_puts(&sserr,protocol);
  substdio_putsflush(&sserr,"\n");
}

static void exit_according_to_child_exit(int exitcode,int child) {
  switch (exitcode) {
    case EXITCODE_CHECKPASSWORD_UNACCEPTABLE:
    case EXITCODE_CHECKPASSWORD_MISUSED:
    case EXITCODE_CHECKPASSWORD_TEMPFAIL:
      logfail(child);
      pfilter_notify(1, 0, PROGNAME, authup_pid);
      if (auth_tries_remaining) return authup_err("badauth");
      authup_die("badauth");
    case EXITCODE_FIXSMTPIO_TIMEOUT:
      authup_die("alarm");
    case EXITCODE_FIXSMTPIO_PARSEFAIL:
      authup_die("control");
  }

  logpass(child);
  pfilter_notify(0, 0, PROGNAME, authup_pid);
  unistd_exit(exitcode);
}

static void logtry(int checkpassword_pid) {
  substdio_puts(&sserr,PROGNAME);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,authup_pid);
  substdio_puts(&sserr," checkpassword ");
  substdio_puts(&sserr,format_pid(checkpassword_pid));
  substdio_puts(&sserr," attempt for ");
  substdio_puts(&sserr,logname.s);
  substdio_putsflush(&sserr,"\n");
}

static void checkpassword(stralloc *username,stralloc *password,stralloc *timestamp) {
  int child;
  int wstat;
  int pi[2];
  char upbuf[SUBSTDIO_OUTSIZE];
  substdio ssup;

  copy(&logname,username); append0(&logname);

  auth_tries_remaining--;

  unistd_close(3);
  if (unistd_pipe(pi) == -1) authup_die("pipe");
  if (pi[0] != 3) authup_die("pipe");
  switch((child = unistd_fork())) {
    case -1:
      authup_die("fork");
    case 0:
      unistd_close(pi[1]);
      sig_pipedefault();
      append0(username);
      logtry(unistd_getpid());
      if (!env_put2("AUTHUP_USER",username->s)) authup_die("nomem");
      unistd_execvp(*childargs,childargs);
      authup_die("fork");
  }
  unistd_close(pi[0]);
  substdio_fdbuf(&ssup,write,pi[1],upbuf,sizeof upbuf);

  append0(username);
  if (substdio_put(&ssup,username->s,username->len) == -1) authup_die("write");
  byte_zero(username->s,username->len);

  append0(password);
  if (substdio_put(&ssup,password->s,password->len) == -1) authup_die("write");
  byte_zero(password->s,password->len);

  append0(timestamp);
  if (substdio_put(&ssup,timestamp->s,timestamp->len) == -1) authup_die("write");
  byte_zero(timestamp->s,timestamp->len);

  if (substdio_flush(&ssup) == -1) authup_die("write");
  unistd_close(pi[1]);
  byte_zero(upbuf,sizeof upbuf);

  if (wait_pid(&wstat,child) == -1) authup_die("wait");
  if (wait_crashed(wstat)) authup_die("crash");

  exit_according_to_child_exit(wait_exitcode(wstat),child);
}

static char unique[FMT_ULONG + FMT_ULONG + 3];

static void pop3_greet() {
  char *s;
  s = unique;
  s += fmt_uint(s,unistd_getpid());
  *s++ = '.';
  s += fmt_ulong(s,(unsigned long) now());
  *s++ = '@';
  *s++ = 0;
  out("+OK <");
  out(unique);
  out(greeting.s);
  out(">\r\n");
  flush();
}

static void pop3_format_capa(stralloc *multiline) {
  cats(multiline,".\r\n");
}

static void pop3_capa(char *arg) {
  out("+OK capability list follows\r\n");
  if (tls_level >= UCSPITLS_AVAILABLE && !in_tls) out("STLS\r\n");
  out("USER\r\n");
  out(capabilities.s);
  flush();
}

static int seenuser = 0;

static void pop3_stls(char *arg) {
  if (tls_level < UCSPITLS_AVAILABLE || in_tls) return pop3_err("STLS not available");
  out("+OK starting TLS negotiation\r\n");
  flush();

  if (!tls_init() || !tls_info(die_nomem)) return authup_err("starttls");
  /* reset state */
  seenuser = 0;

  in_tls = 1;
}

static void pop3_user(char *arg) {
  if (tls_level >= UCSPITLS_REQUIRED && !in_tls) return authup_err("needtls");
  if (!*arg) { pop3_err_syntax(); return; }
  pop3_okay();
  seenuser = 1;
  copys(&username,arg);
}

static void pop3_pass(char *arg) {
  if (!seenuser) { pop3_err_wantuser(); return; }
  if (!*arg) { pop3_err_syntax(); return; }

  copys(&password,arg);
  byte_zero(arg,str_len(arg));

  copys(&timestamp,"<");
  cats(&timestamp,unique);
  cats(&timestamp,greeting.s);
  cats(&timestamp,">");

  checkpassword(&username,&password,&timestamp);
}

static void smtp_greet() {
  out("220 ");
  out(greeting.s);
  out(" ESMTP\r\n");
  flush();
}

static void smtp_helo(char *arg) {
  out("250 ");
  smtp_out(greeting.s);
}

// copy from fixsmtpio_munge.c:change_last_line_fourth_char_to_space()
static void smtp_format_ehlo(stralloc *multiline) {
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

static void smtp_ehlo(char *arg) {
  char *x;
  out("250-"); out(greeting.s); out("\r\n");
  if (tls_level >= UCSPITLS_AVAILABLE && !in_tls) out("250-STARTTLS\r\n");
  out("250-AUTH LOGIN PLAIN\r\n");
  if ((x = env_get("AUTHUP_SASL_BROKEN_CLIENTS")))
    out("250-AUTH=LOGIN PLAIN\r\n");
  out(capabilities.s);
  flush();
}

static void smtp_starttls() {
  if (tls_level < UCSPITLS_AVAILABLE || in_tls) return smtp_out("502 unimplemented (#5.5.1)");
  smtp_out("220 Ready to start TLS (#5.7.0)");

  if (!tls_init() || !tls_info(die_nomem)) return authup_err("starttls");
  /* reset state */
  ssin.p = 0;

  in_tls = 1;
}

static stralloc authin = {0};

static void smtp_authgetl() {
  int i;

  blank(&authin);

  for (;;) {
    if (!stralloc_readyplus(&authin,1)) authup_die("nomem"); /* XXX */
    i = substdio_get(&ssin,authin.s + authin.len,1);
    if (i != 1) authup_die("read");
    if (authin.s[authin.len] == '\n') break;
    ++authin.len;
  }

  if (authin.len > 0) if (authin.s[authin.len - 1] == '\r') --authin.len;
  authin.s[authin.len] = 0;

  if (*authin.s == '*' && *(authin.s + 1) == 0) return authup_err("authabrt");
  if (authin.len == 0) return authup_err("input");
}

static int b64decode2(char *c,int i,stralloc *sa) {
  return b64decode((const unsigned char *)c,i,sa);
}

static void auth_login(char *arg) {
  int r;

  if (*arg) {
    if ((r = b64decode2(arg,str_len(arg),&username)) == 1) return authup_err("input");
  }
  else {
    smtp_out("334 VXNlcm5hbWU6"); /* Username: */
    smtp_authgetl();
    if ((r = b64decode2(authin.s,authin.len,&username)) == 1) return authup_err("input");
  }
  if (r == -1) authup_die("nomem");

  smtp_out("334 UGFzc3dvcmQ6"); /* Password: */

  smtp_authgetl();
  if ((r = b64decode2(authin.s,authin.len,&password)) == 1) return authup_err("input");
  if (r == -1) authup_die("nomem");

  if (!username.len || !password.len) return authup_err("input");
  checkpassword(&username,&password,&timestamp);
}

static stralloc resp = {0};

static void auth_plain(char *arg) {
  int r, id = 0;

  if (*arg) {
    if ((r = b64decode2(arg,str_len(arg),&resp)) == 1) return authup_err("input");
  }
  else {
    smtp_out("334 ");
    smtp_authgetl();
    if ((r = b64decode2(authin.s,authin.len,&resp)) == 1) return authup_err("input");
  }
  if (r == -1) authup_die("nomem");
  append0(&resp);
  while (resp.s[id]) id++; /* ignore authorize-id */

  if (resp.len > id + 1)
    copys(&username,resp.s + id + 1);
  if (resp.len > id + username.len + 2)
    copys(&password,resp.s + id + username.len + 2);

  if (!username.len || !password.len) return authup_err("input");
  checkpassword(&username,&password,&timestamp);
}

static void smtp_auth(char *arg) {
  int i;
  char *cmd = arg;

  if (tls_level >= UCSPITLS_REQUIRED && !in_tls) return authup_err("needtls");

  i = str_chr(cmd,' ');
  arg = cmd + i;
  while (*arg == ' ') ++arg;
  cmd[i] = 0;

  if (case_equals("login",cmd)) return auth_login(arg);
  if (case_equals("plain",cmd)) return auth_plain(arg);
  return authup_err("noauth");
}

static void smtp_help() {
  smtp_out("214 " PROGNAME " home page: " HOMEPAGE);
}

static void smtp_noop() {
  smtp_out("250 ok");
}

static struct commands pop3commands[] = {
  { "stls", pop3_stls, 0 }
, { "user", pop3_user, 0 }
, { "pass", pop3_pass, 0 }
, { "quit", pop3_quit, 0 }
, { "capa", pop3_capa, 0 }
, { "noop", pop3_okay, 0 }
, { 0, pop3_err_authoriz, 0 }
};

static struct commands smtpcommands[] = {
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
  void (*greet)();
  int auth_tries_remaining;
  struct commands *c;
};

static struct protocol p[] = {
  { "pop3", "",     pop3_format_capa, pop3_auth_error, pop3_greet, 1, pop3commands }
, { "smtp", "250-", smtp_format_ehlo, smtp_auth_error, smtp_greet, 5, smtpcommands }
, { 0,      "",     0,                die_usage,       die_usage,  0, 0            }
};

static int control_readgreeting(char *p) {
  stralloc file = {0};
  int retval;

  copys(&file,"control/");
  cats(&file,p);
  cats(&file,"greeting");
  append0(&file);

  retval = control_rldef(&greeting,file.s,1,(char *) 0);
  if (retval != 1) retval = -1;

  append0(&greeting);

  return retval;
}

static int control_readtimeout(char *p) {
  stralloc file = {0};

  copys(&file,"control/timeout");
  cats(&file,p);
  cats(&file,"d");
  append0(&file);

  return control_readint(&timeout,file.s);
}

static int control_readcapabilities(struct protocol p) {
  stralloc file = {0};
  stralloc lines = {0};
  int linestart;
  int pos;

  copys(&file,"control/");
  cats(&file,p.name);
  cats(&file,"capabilities");
  append0(&file);

  if (control_readfile(&lines,file.s,0) != 1) return -1;

  blank(&capabilities);
  for (linestart = 0, pos = 0; pos < lines.len; pos++) {
    if (lines.s[pos] == '\0') {
      cats(&capabilities,p.cap_prefix);
      cats(&capabilities,lines.s+linestart);
      cats(&capabilities,"\r\n");
      linestart = pos + 1;
    }
  }
  p.cap_format_response(&capabilities);
  append0(&capabilities);

  return 1;
}

static void doprotocol(struct protocol p) {
  protocol_error = p.error;

  logstart(p.name);

  if (auth_tries_remaining == 0) auth_tries_remaining = p.auth_tries_remaining;
  if (unistd_chdir(auto_qmail) == -1) authup_die("control");
  if (control_init() == -1) authup_die("control");
  if (control_readgreeting(p.name) == -1) authup_die("control");
  if (control_readtimeout(p.name) == -1) authup_die("control");
  if (control_readcapabilities(p) == -1) authup_die("control");
  p.greet();
  commands(&ssin,p.c);
  authup_die("protocol");
}

int main(int argc,char **argv) {
  char *protocol;
  int opt;
  int i;

  sig_alarmcatch(die);
  sig_pipeignore();

  stralloc_set_die(die_nomem);

  auth_tries_remaining = 0;
  while ((opt = getopt(argc,argv,"t:")) != opteof) {
    switch (opt) {
      case 't':
        if (!scan_ulong(optarg,&auth_tries_remaining)) die_usage();
        break;
      default:
        die_usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (!*argv) die_usage();

  protocol = argv[0];
  if (!protocol) die_usage();

  childargs = argv + 1;
  if (!*childargs) die_usage();

  tls_level = ucspitls_level_configured();

  authup_pid = format_pid(unistd_getpid());

  for (i = 0; p[i].name; ++i)
    if (case_equals(p[i].name,protocol))
      doprotocol(p[i]);
  die_usage();
}

#include "fixsmtpio.h"
#include "fixsmtpio-filter.h"
#include "fixsmtpio-proxy.h"

void die() { _exit(1); }

char sserrbuf[SUBSTDIO_OUTSIZE];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void dieerrflush(char *s) {
  substdio_puts(&sserr,PROGNAME ": ");
  substdio_puts(&sserr,s);
  substdio_putsflush(&sserr,"\n");
  die();
}

void die_usage() { dieerrflush("usage: " PROGNAME " prog [ arg ... ]"); }
void die_control(){dieerrflush("unable to read controls"); }
void die_pipe()  { dieerrflush("unable to open pipe"); }
void die_fork()  { dieerrflush("unable to fork"); }
void die_exec()  { dieerrflush("unable to exec"); }
void die_wait()  { dieerrflush("unable to wait for child"); }
void die_crash() { dieerrflush("aack, child crashed"); }
void die_read()  { dieerrflush("unable to read"); }
void die_write() { dieerrflush("unable to write"); }
void die_nomem() { dieerrflush("out of memory"); }

void cat(stralloc *to,stralloc *from) { if (!stralloc_cat(to,from)) die_nomem(); }
void catb(stralloc *to,char *buf,int len) { if (!stralloc_catb(to,buf,len)) die_nomem(); }
void cats(stralloc *to,char *from) { if (!stralloc_cats(to,from)) die_nomem(); }
void copy(stralloc *to,stralloc *from) { if (!stralloc_copy(to,from)) die_nomem(); }
void copyb(stralloc *to,char *buf,int len) { if (!stralloc_copyb(to,buf,len)) die_nomem(); }
void copys(stralloc *to,char *from) { if (!stralloc_copys(to,from)) die_nomem(); }
int starts(stralloc *haystack,char *needle) { return stralloc_starts(haystack,needle); }
void blank(stralloc *sa) { copys(sa,""); }

void use_as_stdin(int fd)  { if (fd_move(0,fd) == -1) die_pipe(); }
void use_as_stdout(int fd) { if (fd_move(1,fd) == -1) die_pipe(); }

void make_pipe(int *from,int *to) {
  int pi[2];
  if (pipe(pi) == -1) die_pipe();
  *from = pi[0];
  *to = pi[1];
}

void be_proxied(int from_proxy,int to_proxy,
                int from_server,int to_server,
                char **argv) {
  close(from_server);
  close(to_server);
  use_as_stdin(from_proxy);
  use_as_stdout(to_proxy);
  execvp(*argv,argv);
  die_exec();
}

void teardown_and_exit(int exitcode,int child,filter_rule *rules,
                       int from_server,int to_server) {
  int wstat;

  close(from_server);
  close(to_server);

  if (wait_pid(&wstat,child) == -1) die_wait();
  if (wait_crashed(wstat)) die_crash();

  if (exitcode == EXIT_LATER_NORMALLY) _exit(wait_exitcode(wstat));
  else _exit(exitcode);
}

void be_proxy(int from_client,int to_client,
              int from_proxy,int to_proxy,
              int from_server,int to_server,
              stralloc *greeting,filter_rule *rules,
              int child) {
  int exitcode;

  close(from_proxy);
  close(to_proxy);
  exitcode = read_and_process_until_either_end_closes(from_client,to_server,
                                                      from_server,to_client,
                                                      greeting,rules);
  teardown_and_exit(exitcode,child,rules,from_server,to_server);
}

void load_smtp_greeting(stralloc *greeting,char *configfile) {
  if (control_init() == -1) die_control();
  if (control_rldef(greeting,configfile,1,(char *) 0) != 1) die_control();
}

void cd_var_qmail() {
  if (chdir(auto_qmail) == -1) die_control();
}

void main_program(int argc,char **argv) {
  stralloc greeting = {0};
  filter_rule *rules;
  int from_client = 0;
  int from_proxy, to_server;
  int from_server, to_proxy;
  int to_client = 1;
  int child;

  argv++; if (!*argv) die_usage();

  cd_var_qmail();
  load_smtp_greeting(&greeting,"control/smtpgreeting");
  rules = load_filter_rules();

  make_pipe(&from_proxy,&to_server);
  make_pipe(&from_server,&to_proxy);

  if ((child = fork()))
    be_proxy(from_client,to_client,
             from_proxy,to_proxy,
             from_server,to_server,
             &greeting,rules,
             child);
  else if (child == 0)
    be_proxied(from_proxy,to_proxy,
               from_server,to_server,
               argv);
  else
    die_fork();
}

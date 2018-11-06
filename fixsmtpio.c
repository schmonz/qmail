#include "fixsmtpio.h"
#include "fixsmtpio_die.h"
#include "fixsmtpio_filter.h"
#include "fixsmtpio_proxy.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_unistd.h"

void use_as_stdin(int fd)  { if (fd_move(0,fd) == -1) die_pipe(); }
void use_as_stdout(int fd) { if (fd_move(1,fd) == -1) die_pipe(); }

void make_pipe(int *from,int *to) {
  int pi[2];
  if (unistd_pipe(pi) == -1) die_pipe();
  *from = pi[0];
  *to = pi[1];
}

void be_proxied(int from_proxy,int to_proxy,
                int from_proxied,int to_proxied,
                char **argv) {
  unistd_close(from_proxied);
  unistd_close(to_proxied);
  use_as_stdin(from_proxy);
  use_as_stdout(to_proxy);
  unistd_execvp(*argv,argv);
  die_exec();
}

void teardown_and_exit(int exitcode,int child,filter_rule *rules,
                       int from_server,int to_server) {
  int wstat;

  unistd_close(from_server);
  unistd_close(to_server);

  if (wait_pid(&wstat,child) == -1) die_wait();
  if (wait_crashed(wstat)) die_crash();

  if (exitcode == EXIT_LATER_NORMALLY) unistd_exit(wait_exitcode(wstat));
  else unistd_exit(exitcode);
}

void be_proxy(int from_client,int to_client,
              int from_proxy,int to_proxy,
              int from_server,int to_server,
              stralloc *greeting,filter_rule *rules,
              int proxied) {
  int exitcode;

  unistd_close(from_proxy);
  unistd_close(to_proxy);
  exitcode = read_and_process_until_either_end_closes(from_client,to_server,
                                                      from_server,to_client,
                                                      greeting,rules);
  teardown_and_exit(exitcode,proxied,rules,from_server,to_server);
}

void load_smtp_greeting(stralloc *greeting,char *configfile) {
  if (control_init() == -1) die_control();
  if (control_rldef(greeting,configfile,1,(char *) 0) != 1) die_control();
}

void cd_var_qmail() {
  if (unistd_chdir(auto_qmail) == -1) die_control();
}

int main(int argc,char **argv) {
  stralloc greeting = {0};
  filter_rule *rules;
  int from_client = 0;
  int from_proxy, to_server;
  int from_server, to_proxy;
  int to_client = 1;
  int proxied;

  argv++; if (!*argv) die_usage();

  stralloc_set_die(die_nomem);

  cd_var_qmail();
  load_smtp_greeting(&greeting,"control/smtpgreeting");
  rules = load_filter_rules();

  make_pipe(&from_proxy,&to_server);
  make_pipe(&from_server,&to_proxy);

  if ((proxied = unistd_fork()))
    be_proxy(from_client,to_client,
             from_proxy,to_proxy,
             from_server,to_server,
             &greeting,rules,
             proxied);
  else if (proxied == 0)
    be_proxied(from_proxy,to_proxy,
               from_server,to_server,
               argv);
  else
    die_fork();
}

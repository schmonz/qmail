#include "getln.h"
#include "readwrite.h"
#include "sig.h"
#include "stralloc.h"
#include "substdio.h"
#include "wait.h"

char **childargs;

void die() { _exit(1); }

char sserrbuf[128];
substdio sserr = SUBSTDIO_FDBUF(write,2,sserrbuf,sizeof sserrbuf);

void errflush(char *s) {
  substdio_puts(&sserr,s);
  substdio_flush(&sserr);
}

void die_usage() { errflush("usage: qmail-reup subprogram\n"); die(); }
void die_pipe() { errflush("qmail-reup unable to open pipe\n"); die(); }
void die_fork() { errflush("qmail-reup unable to fork\n"); die(); }
void die_read() { errflush("qmail-reup unable to read from pipe\n"); die(); }
void die_nomem() { errflush("qmail-reup out of memory\n"); die(); }

char ssoutbuf[128];
substdio ssout = SUBSTDIO_FDBUF(write,1,ssoutbuf,sizeof ssoutbuf);

void putsflush(char *s) {
  substdio_puts(&ssout,s);
  substdio_flush(&ssout);
}

char ssinbuf[128];
substdio ssin = SUBSTDIO_FDBUF(read,0,ssinbuf,sizeof ssinbuf);

int read_line(stralloc *into) {
  int match;
  if (getln(&ssin,into,&match,'\n') == -1) die_read();
  if (!stralloc_0(into)) die_nomem();
  if (match == 0) return 0; // XXX allow partial final line?

  return 1;
}

void filter_output(int attempt) {
  stralloc line = {0};
  for (int lineno = 0; read_line(&line); lineno++) {
    if (lineno > 0 || attempt == 0) putsflush(line.s);
  }

  /*
   * XXX is "hide first line on subsequent attempts" the right idea?
   * what if the contents of the first line are other than expected?
   * what if the total output is one line?
   */
}

void write_stdout_to(int fd) {
  if (fd_move(1,fd) == -1) die_pipe();
}

void read_stdin_from(int fd) {
  if (fd_move(0,fd) == -1) die_pipe();
}

int get_unused_fd() {
  return 7;
}

void save_original_stdin(int temp_fd) {
  if (fd_copy(temp_fd,0) == -1) die_pipe();
}

void restore_original_stdin(int temp_fd) {
  if (fd_move(0,temp_fd) == -1) die_pipe();
}

int try(int attempt) {
  int child;
  int wstat;
  int pi[2];
  int temp_fd;

  if (pipe(pi) == -1) die_pipe();

  switch (child = fork()) {
    case -1:
      die_fork();
      break;
    case 0:
      close(pi[0]);
      write_stdout_to(pi[1]);
      execvp(*childargs,childargs);
      die();
  }
  close(pi[1]);

  temp_fd = get_unused_fd();
  save_original_stdin(temp_fd);
  read_stdin_from(pi[0]);
  filter_output(attempt);
  restore_original_stdin(temp_fd);

  if (wait_pid(&wstat,child) == -1) die();
  if (wait_crashed(wstat)) die();

  return wait_exitcode(wstat);
}

int stop_trying(int exitcode) {
  switch (exitcode) {
    case 0:
      return 1;
    default:
      return 0;
  }
}

int main(int argc,char **argv) {
  int exitcode;

  childargs = argv + 1;
  if (!*childargs) die_usage();

  sig_alarmcatch(die);
  sig_pipeignore();

  for (int i = 0; i < 3; i++) {
    exitcode = try(i);
    if (stop_trying(exitcode)) _exit(exitcode);
  }

  _exit(exitcode);
}

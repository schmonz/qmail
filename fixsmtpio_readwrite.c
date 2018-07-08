#include "fixsmtpio_readwrite.h"
#include "fixsmtpio_common.h"
#include "error.h"
#include "readwrite.h"
#include "select.h"

fd_set fds;

static void want_to_read(int fd1,int fd2) {
  FD_ZERO(&fds);
  FD_SET(fd1,&fds);
  FD_SET(fd2,&fds);
}

int can_read(int fd) { return FD_ISSET(fd,&fds); }

static int max(int a,int b) { return a > b ? a : b; }

int block_efficiently_until_can_read_either(int fd1,int fd2) {
  int ready;
  want_to_read(fd1,fd2);
  ready = select(1+max(fd1,fd2),&fds,(fd_set *)0,(fd_set *)0,(struct timeval *) 0);
  if (ready == -1 && errno != error_intr) die_read();
  return ready;
}

static int saferead(int fd,char *buf,int len) {
  int r;
  r = read(fd,buf,len);
  if (r == -1) if (errno != error_intr) die_read();
  return r;
}

int safeappend(stralloc *sa,int fd,char *buf,int len) {
  int r;
  r = saferead(fd,buf,len);
  catb(sa,buf,r);
  return r;
}

void safewrite(int fd,stralloc *sa) {
  if (write(fd,sa->s,sa->len) == -1) die_write();
  blank(sa);
}

#include "case.h"
#include "env.h"
#include "fd.h"
#include "readwrite.h"
#include "scan.h"
#include "stralloc.h"

#include "acceptutils_ucspitls.h"

int ucspitls_level(void) {
  char *ucspitls = env_get("UCSPITLS");
  env_unset("UCSPITLS");
  if (!ucspitls || !case_diffs(ucspitls,"-")) return UCSPITLS_UNAVAILABLE;
  if (!case_diffs(ucspitls,"!")) return UCSPITLS_REQUIRED;
  return UCSPITLS_AVAILABLE;
}

static int get_fd_for(char *name) {
  unsigned long fd;
  char *fdstr;

  if (!(fdstr = env_get(name))) return 0;
  if (!scan_ulong(fdstr,&fd)) return 0;

  return (int)fd;
}

static int notify_control_socket() {
  unsigned int fd = get_fd_for("SSLCTLFD");

  if (!fd) return 0;
  if (write(fd, "Y", 1) < 1) return 0;

  return 1;
}

static int adjust_read_fd() {
  unsigned int fd = get_fd_for("SSLREADFD");

  if (!fd) return 0;
  if (fd_move(0,fd) == -1) return 0;

  return 1;
}

static int adjust_write_fd() {
  unsigned int fd = get_fd_for("SSLWRITEFD");

  if (!fd) return 0;
  if (fd_move(1,fd) == -1) return 0;

  return 1;
}

int starttls_init(void) {
  return notify_control_socket() && adjust_read_fd() && adjust_write_fd();
}

int starttls_info(void (*die_nomem)()) {
  unsigned long fd;
  char *fdstr;
  char envbuf[8192];
  char *x;
  int j;

  stralloc ssl_env   = {0};
  stralloc ssl_parm  = {0};
  stralloc ssl_value = {0};

  if (!(fdstr=env_get("SSLCTLFD")))
    return 0;
  if (!scan_ulong(fdstr,&fd))
    return 0;

  while ((j=read(fd,envbuf,8192)) > 0 ) {
    stralloc_catb(&ssl_env,envbuf,j);
      if (ssl_env.len >= 2 && ssl_env.s[ssl_env.len-2]==0 && ssl_env.s[ssl_env.len-1]==0)
        break;
  }
  if (j <= 0)
    die_nomem();

  x = ssl_env.s;

  for (j=0;j < ssl_env.len-1;++j) {
    if ( *x != '=' ) {
      if (!stralloc_catb(&ssl_parm,x,1)) die_nomem();
      x++; }
    else {
      if (!stralloc_0(&ssl_parm)) die_nomem();
      x++;

      for (;j < ssl_env.len-j-1;++j) {
        if ( *x != '\0' ) {
          if (!stralloc_catb(&ssl_value,x,1)) die_nomem();
          x++; }
        else {
          if (!stralloc_0(&ssl_value)) die_nomem();
          x++;
          if (!env_put2(ssl_parm.s,ssl_value.s)) die_nomem();
          ssl_parm.len = 0;
          ssl_value.len = 0;
          break;
        }
      }
    }
  }
  return j;
}

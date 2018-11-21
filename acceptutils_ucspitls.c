#include "case.h"
#include "env.h"
#include "fd.h"
#include "readwrite.h"
#include "scan.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_ucspitls.h"

int ucspitls_level(void) {
  char *ucspitls = env_get("UCSPITLS");
  char *disabletls = env_get("DISABLETLS");
  env_unset("UCSPITLS");
  env_unset("DISABLETLS");
  if (disabletls || !ucspitls || !case_diffs(ucspitls,"-"))
    return UCSPITLS_UNAVAILABLE;
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

int tls_init(void) {
  return notify_control_socket() && adjust_read_fd() && adjust_write_fd();
}

int tls_info(void (*die_nomem)(const char *caller,const char *alloc_fn)) {
  unsigned long fd;
  char envbuf[8192];
  char *x;
  int j;

  stralloc ssl_env   = {0};
  stralloc ssl_parm  = {0};
  stralloc ssl_value = {0};

  fd = get_fd_for("SSLCTLFD");
  if (!fd) return 0;

  while ((j=read(fd,envbuf,8192)) > 0 ) {
    catb(&ssl_env,envbuf,j);
      if (ssl_env.len >= 2 && ssl_env.s[ssl_env.len-2]==0 && ssl_env.s[ssl_env.len-1]==0)
        break;
  }
  if (j <= 0)
    die_nomem(__func__,"read");

  x = ssl_env.s;

  for (j=0;j < ssl_env.len-1;++j) {
    if ( *x != '=' ) {
      catb(&ssl_parm,x,1);
      x++;
    } else {
      append0(&ssl_parm);
      x++;

      for (;j < ssl_env.len-j-1;++j) {
        if ( *x != '\0' ) {
          catb(&ssl_value,x,1);
          x++;
        } else {
          append0(&ssl_value);
          x++;
          if (!env_put2(ssl_parm.s,ssl_value.s))
            die_nomem(__func__,"env_put2");
          ssl_parm.len = 0;
          ssl_value.len = 0;
          break;
        }
      }
    }
  }
  return j;
}

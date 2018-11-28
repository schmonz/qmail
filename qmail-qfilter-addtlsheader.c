#include "datetime.h"
#include "date822fmt.h"
#include "env.h"
#include "now.h"
#include "readwrite.h"
#include "substdio.h"

static char inbuf[SUBSTDIO_INSIZE];
static substdio ssin = SUBSTDIO_FDBUF(read,0,inbuf,sizeof(inbuf));
static char outbuf[SUBSTDIO_OUTSIZE];
static substdio ssout = SUBSTDIO_FDBUF(write,1,outbuf,sizeof(outbuf));

static void out(char *s) { substdio_puts(&ssout,s); }

static char datebuf[DATE822FMT];

static void set_now(char *datebuf) {
  struct datetime dt;
  datetime_tai(&dt,now());
  date822fmt(datebuf,&dt);
}

static void perhaps_write_tls_header() {
  char *ssl_cipher, *ssl_protocol, *authup_user;

  if ((ssl_cipher = env_get("SSL_CIPHER"))
    && (ssl_protocol = env_get("SSL_PROTOCOL"))) {
    out("Received: (ucspitls");
    if (env_get("FIXSMTPIOTLS")) {
      out(" acceptutils fixsmtpio");
    } else if ((authup_user = env_get("AUTHUP_USER"))) {
      out(" acceptutils authup ");
      out(authup_user);
    }
    out(  " "); out(ssl_protocol);
    out(  " "); out(ssl_cipher);
    set_now(datebuf);
    out("); "); out(datebuf);
    substdio_flush(&ssout);
  }
}

static void copy_stdin_to_stdout() {
  int i;

  while ((i = substdio_get(&ssin,inbuf,sizeof(inbuf))) > 0)
    substdio_putflush(&ssout,inbuf,i);
}

int main(void) {
  perhaps_write_tls_header();
  copy_stdin_to_stdout();
  return 0;
}

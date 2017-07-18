#include <unistd.h>
#include "byte.h"
#include "case.h"
#include "control.h"
#include "env.h"
#include "fmt.h"
#include "getln.h"
#include "str.h"
#include "stralloc.h"
#include "substdio.h"

extern void die_control();
extern void die_nomem();

static void viruscan_log_rejection(char *signature)
{
  char errbuf[SUBSTDIO_OUTSIZE];
  substdio sserr;
  char *remoteip;
  char *smtpdpid;
  char qfilterpid[FMT_ULONG];

  substdio_fdbuf(&sserr,write,2,errbuf,sizeof(errbuf));

  remoteip = env_get("TCPREMOTEIP");
  if (!remoteip) remoteip = "unknown";
  smtpdpid = env_get("QMAILPPID");
  str_copy(qfilterpid + fmt_ulong(qfilterpid,getppid()),"");

  substdio_puts(&sserr,"qfilter: viruscan ");
  if (smtpdpid) substdio_puts(&sserr,smtpdpid);
  else substdio_puts(&sserr,qfilterpid);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,remoteip);
  substdio_puts(&sserr," ");
  substdio_puts(&sserr,signature);
  substdio_puts(&sserr,"\n");
  substdio_flush(&sserr);
}

static int viruscan_reject_line(stralloc *line)
{
  int sigsok, i, j;
  stralloc sigs = {0};
  sigsok = control_readfile(&sigs,"control/signatures",0);
  if (sigsok == -1) die_control();

  j = 0;
  for (i = 0; i < sigs.len; i++) if (!sigs.s[i]) {
    if (i-j < line->len)
      if (!str_diffn(line->s,sigs.s+j,i-j)) {
        viruscan_log_rejection(sigs.s);
        return 1;
      }
    j = i+1;
  }

  return 0;
}

static int linespastheader;
static char linetype;
static int flagexecutable;
static int flagqsbmf;
static stralloc line = {0};
static stralloc content = {0};
static stralloc boundary = {0};
static int boundary_start;

static void put(ch)
char *ch;
{
  char *cp, *cpstart, *cpafter;
  unsigned int len;

  if (line.len < 1024)
    if (!stralloc_catb(&line,ch,1)) die_nomem();

  if (*ch == '\n') {
    if (linespastheader == 0) {
      if (line.len == 1) {
        linespastheader = 1;
        if (flagqsbmf) {
          flagqsbmf = 0;
          linespastheader = 0;
        }
        if (content.len) { /* MIME header */
          cp = content.s;
          len = content.len;
          while (len && (*cp == ' ' || *cp == '\t')) { ++cp; --len; }
          cpstart = cp;
          if (len && *cp == '"') { /* might be commented */
            ++cp; --len; cpstart = cp;
            while (len && *cp != '"') { ++cp; --len; }
          } else {
            while (len && *cp != ' ' && *cp != '\t' && *cp != ';') {
              ++cp; --len;
            }
          }
          if (!case_diffb(cpstart,cp-cpstart,"message/rfc822"))
            linespastheader = 0;

          cpafter = content.s+content.len;
          while((cp += byte_chr(cp,cpafter-cp,';')) != cpafter) {
            ++cp;
            while (cp < cpafter && (*cp == ' ' || *cp == '\t')) ++cp;
            if (case_startb(cp,cpafter - cp,"boundary=")) {
              cp += 9; /* after boundary= */
              if (cp < cpafter && *cp == '"') {
                ++cp;
                cpstart = cp;
                while (cp < cpafter && *cp != '"') ++cp;
              } else {
                cpstart = cp;
                while (cp < cpafter &&
                       *cp != ';' && *cp != ' ' && *cp != '\t') ++cp;
              }
              /* push the current boundary.  Append a null and remember start. */
              if (!stralloc_0(&boundary)) die_nomem();
              boundary_start = boundary.len;
              if (!stralloc_cats(&boundary,"--")) die_nomem();
              if (!stralloc_catb(&boundary,cpstart,cp-cpstart))
                die_nomem();
              break;
            }
          }
        }
      } else { /* non-blank header line */
        if ((*line.s == ' ' || *line.s == '\t')) {
          switch(linetype) {
          case 'C': if (!stralloc_catb(&content,line.s,line.len-1)) die_nomem(); break;
          default: break;
          }
        } else {
          if (case_startb(line.s,line.len,"content-type:")) {
            if (!stralloc_copyb(&content,line.s+13,line.len-14)) die_nomem();
            linetype = 'C';
          } else {
            linetype = ' ';
          }
        }
      }
    } else { /* non-header line */
      if (boundary.len-boundary_start && *line.s == '-' && line.len > (boundary.len-boundary_start) &&
          !str_diffn(line.s,boundary.s+boundary_start,boundary.len-boundary_start)) { /* matches a boundary */
        if (line.len > boundary.len-boundary_start + 2 &&
            line.s[boundary.len-boundary_start+0] == '-' &&
            line.s[boundary.len-boundary_start+1] == '-') {
          /* XXXX - pop the boundary here */
          if (boundary_start) boundary.len = boundary_start - 1;
          boundary_start = boundary.len;
          while(boundary_start--) if (!boundary.s[boundary_start]) break;
          boundary_start++;
          linespastheader = 2;
        } else {
          linespastheader = 0;
        }
      } else if (linespastheader == 1) { /* first line -- match a signature? */
        if (/*mailfrom.s[0] == '\0' && */
                str_start(line.s,"Hi. This is the "))
          flagqsbmf = 1;
        else if (/*mailfrom.s[0] == '\0' && */
                str_start(line.s,"This message was created automatically by mail delivery software"))
          flagqsbmf = 1;
        else if (viruscan_reject_line(&line)) {
          flagexecutable = 1;
        }
        linespastheader = 2;
      }
      if (flagqsbmf && str_start(line.s,"---")) {
        linespastheader = 0;
      }
    }
    line.len = 0;
  }
}

int viruscan_reject_attachment()
{
  char inbuf[SUBSTDIO_INSIZE];
  substdio ssin;
  char ch;

  substdio_fdbuf(&ssin,read,0,inbuf,sizeof(inbuf));

  for (;;) {
    if (1 != substdio_get(&ssin,&ch,1)) break;
    put(&ch);
    if (flagexecutable)
      return 1;
  }

  return 0;
}

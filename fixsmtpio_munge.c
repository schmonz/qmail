#include "fixsmtpio_munge.h"
#include "fixsmtpio_common.h"
#include "acceptutils_ucspitls.h"

void munge_exitcode(int *exitcode,filter_rule *rule) {
  if (rule->exitcode != EXIT_LATER_NORMALLY) *exitcode = rule->exitcode;
}

void munge_greeting(stralloc *response,int lineno,stralloc *greeting,
                    int tls_level,int in_tls) {
  copys(response,"220 "); cat(response,greeting); cats(response," ESMTP");
}

void munge_helo(stralloc *response,int lineno,stralloc *greeting,
                int tls_level,int in_tls) {
  copys(response,"250 "); cat(response,greeting);
}

static int is_starttls_line(stralloc *response) {
  return stralloc_starts(response,"250-STARTTLS\r\n")
      || stralloc_starts(response,"250 STARTTLS\r\n");
}

void munge_ehlo(stralloc *response,int lineno,stralloc *greeting,
                int tls_level,int in_tls) {
  switch (lineno) {
    case 0:
      munge_helo(response,lineno,greeting,tls_level,in_tls);
      break;
    case 1:
      if (is_starttls_line(response)) blank(response);
      if (tls_level && !in_tls && !env_get("AUTHUP_USER"))
        prepends(response,"250-STARTTLS\r\n");
      break;
    default:
      if (is_starttls_line(response)) blank(response);
      break;
  }
}

void munge_help(stralloc *response,int lineno,stralloc *greeting,
                int tls_level,int in_tls) {
  stralloc munged = {0};
  copys(&munged,"214 " PROGNAME " home page: " HOMEPAGE "\r\n");
  cat(&munged,response);
  copy(response,&munged);
}

void munge_quit(stralloc *response,int lineno,stralloc *greeting,
                int tls_level,int in_tls) {
  copys(response,"221 "); cat(response,greeting);
}

void change_every_line_fourth_char_to_dash(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = 0; i < multiline->len; i++) {
    if (multiline->s[i] == '\n') pos = -1;
    if (pos == 3) multiline->s[i] = '-';
    pos++;
  }
}

// copy to with authup.c:smtp_ehlo_format()
void change_last_line_fourth_char_to_space(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = multiline->len - 2; i >= 0; i--) {
    if (multiline->s[i] == '\n') {
      pos = i + 1;
      break;
    }
  }
  multiline->s[pos+3] = ' ';
}

void reformat_multiline_response(stralloc *response) {
  change_every_line_fourth_char_to_dash(response);
  change_last_line_fourth_char_to_space(response);
}

int event_matches(char *s,const char *s2) {
  if (!s || !s2) return 0;
  if (!str_len(s) || !str_len(s2)) return 0;
  return !case_diffs(s,s2);
}

struct munge_command {
  char *event;
  void (*munger)();
};

struct munge_command m[] = {
  { EVENT_GREETING, munge_greeting }
, { "ehlo", munge_ehlo }
, { "helo", munge_helo }
, { "help", munge_help }
, { "quit", munge_quit }
, { 0, 0 }
};

void *munge_line_fn(const char *event) {
  int i;
  for (i = 0; m[i].event; ++i)
    if (event_matches(m[i].event,event))
      return m[i].munger;
  return 0;
}

void munge_line_internally(stralloc *line,int lineno,
                           stralloc *greeting,const char *event,
                           int tls_level,int in_tls) {
  void (*munger)() = munge_line_fn(event);
  if (munger) munger(line,lineno,greeting,tls_level,in_tls);
}

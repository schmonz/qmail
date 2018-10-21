#include "fixsmtpio_munge.h"
#include "fixsmtpio_common.h"

void munge_exitcode(int *exitcode,filter_rule *rule) {
  if (rule->exitcode != EXIT_LATER_NORMALLY) *exitcode = rule->exitcode;
}

void munge_greeting(stralloc *response,int lineno,stralloc *greeting) {
  copys(response,"220 "); cat(response,greeting);
}

void munge_helo(stralloc *response,int lineno,stralloc *greeting) {
  copys(response,"250 "); cat(response,greeting);
}

void munge_ehlo(stralloc *response,int lineno,stralloc *greeting) {
  if (lineno) return; munge_helo(response,lineno,greeting);
}

void munge_help(stralloc *response,int lineno,stralloc *greeting) {
  stralloc munged = {0};
  copys(&munged,"214 " PROGNAME " home page: " HOMEPAGE "\r\n");
  cat(&munged,response);
  copy(response,&munged);
}

void munge_quit(stralloc *response,int lineno,stralloc *greeting) {
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
                           stralloc *greeting,const char *event) {
  void (*munger)() = munge_line_fn(event);
  if (munger) munger(line,lineno,greeting);
}

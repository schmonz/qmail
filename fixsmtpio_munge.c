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

/*
 * input is null
 * input is empty
 * input is less than four chars long
 * input is exactly four chars long
 * input is longer than four, but still one line
 * input is multiple lines, first one long enough, second one not
 * input is multiple lines, first one short, second one long enough
 * input is multiple lines, first one long enough, second one not, third one long enough
 */
static void change_every_line_fourth_char_to_dash(stralloc *multiline) {
  int pos = 0;
  int i;
  for (i = 0; i < multiline->len; i++) {
    if (multiline->s[i] == '\n') pos = -1;
    if (pos == 3) multiline->s[i] = '-';
    pos++;
  }
}

/*
 * input is null
 * input is empty
 * input is less than four chars long
 * input is exactly four chars long
 * input is longer than four, but still one line
 * input is multiple lines, first one long enough, second one not
 * input is multiple lines, first one short, second one long enough
 * input is multiple lines, first one long enough, second one not, third one long enough
 */
static void change_last_line_fourth_char_to_space(stralloc *multiline) {
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

/*
 * s is NULL
 * s is not 0-terminated
 * s is empty
 * sa is NULL
 * sa->len is 0
 * strings differ, don't match
 * strings same, do match
 * strings same except case, do match
 */
int event_matches(char *s,const char *s2) {
  if (!str_len(s2)) return 0;
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

static void *munge_line_fn(const char *event) {
  int i;
  for (i = 0; m[i].event; ++i)
    if (event_matches(m[i].event,event))
      return m[i].munger;
  return 0;
}

/*
 not requests, response lines!

 ("250 word up, kids", 0, "yo.sup.local", "word") -> "250 word up, kids"

 ("250-applesauce",     0, "yo.sup.local", "ehlo") -> "250-yo.sup.local"
 ("250-STARTSOMETHING", 1, "yo.sup.local", "ehlo") -> "250-STARTSOMETHING"
 ("250 ENDSOMETHING",   2, "yo.sup.local", "ehlo") -> "250 ENDSOMETHING"

 ("250 applesauce",     0, "yo.sup.local", "helo") -> "250-yo.sup.local"

 ("214 ask your grandmother", 0, "yo.sup.local", "help") -> "214 https://....\r\n214 ask your grandmother\r\n"

 ("221 get outta here", 0, "yo.sup.local", "quit") -> "221 yo.sup.local"
 */
void munge_line_internally(stralloc *line,int lineno,
                           stralloc *greeting,const char *event) {
  void (*munger)() = munge_line_fn(event);
  if (munger) munger(line,lineno,greeting);
}

#ifndef _FIXSMTPIO_FILTER_H_
#define _FIXSMTPIO_FILTER_H_

#include "fixsmtpio.h"

typedef struct filter_rule {
  struct filter_rule *next;
  char *env;
  char *event;
  char *request_prepend;
  char *response_line_glob;
  int   exitcode;
  char *response;
} filter_rule;

int want_munge_internally(char *);
int want_leave_line_as_is(char *);
int envvar_exists_if_needed(char *);

filter_rule * load_filter_rules(void);
int filter_rule_applies(filter_rule *,const char *);

int event_matches(char *,const char *);

void munge_response(stralloc *,int *,stralloc *,filter_rule *,const char *,int,int);
void munge_response_line(int,stralloc *,int *,stralloc *,filter_rule *,const char *,int,int);
filter_rule *prepend_rule(filter_rule *,filter_rule *);

#endif

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

extern int want_munge_internally(char *);
extern filter_rule * load_filter_rules(void);
extern int filter_rule_applies(filter_rule *,const char *);

extern int event_matches(char *,const char *);

extern void munge_response(stralloc *,int *,stralloc *,filter_rule *,const char *);
extern void munge_response_line(int,stralloc *,int *,stralloc *,filter_rule *,const char *);

#endif

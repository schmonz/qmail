#include "qmail-fixsmtpio.h"

typedef struct filter_rule {
  struct filter_rule *next;
  char *env;
  char *event;
  char *request_prepend;
  char *response_line_glob;
  int   exitcode;
  char *response;
} filter_rule;

extern filter_rule * load_filter_rules(void);
extern void unload_filter_rules(filter_rule *);
extern int filter_rule_applies(filter_rule *,stralloc *);

extern int verb_matches(char *,stralloc *);

extern void munge_response(stralloc *,int *,stralloc *,filter_rule *,stralloc *);
extern void munge_response_line(stralloc *,int,int *,stralloc *,filter_rule *,stralloc *);

extern void strip_last_eol(stralloc *);

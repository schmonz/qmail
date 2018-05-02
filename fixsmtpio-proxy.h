#include "fixsmtpio.h"
#include "fixsmtpio-filter.h"

typedef struct request_response {
  stralloc *client_request;
  stralloc *client_verb;
  stralloc *client_arg;
  stralloc *proxy_request;
  stralloc *server_response;
  stralloc *proxy_response;
  int       proxy_exitcode;
} request_response;

extern substdio sserr;

extern void strip_last_eol(stralloc *);

extern int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *);

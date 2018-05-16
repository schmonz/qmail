#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"

typedef struct proxied_request {
  stralloc *client_request;
  stralloc *client_verb;
  stralloc *client_arg;
  stralloc *proxy_request;
} proxied_request;

typedef struct proxied_response {
  stralloc *server_response;
  stralloc *proxy_response;
  int       proxy_exitcode;
} proxied_response;

extern substdio sserr;

extern void strip_last_eol(stralloc *);
extern int is_entire_line(stralloc *);
extern int is_last_line_of_response(stralloc *);
extern void parse_client_request(stralloc *,stralloc *,stralloc *);
extern void get_one_response(stralloc *,stralloc *);
extern void proxied_response_init(proxied_response *);
extern int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *);

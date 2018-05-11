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
extern int could_be_final_response_line(stralloc *);
extern void parse_client_request(stralloc *,stralloc *,stralloc *);
extern int handle_server_response(stralloc *,filter_rule *,char *,proxied_response *,int *,int *);
extern void proxied_response_init(proxied_response *);
extern int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *);

#include "qmail-fixsmtpio.h"

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

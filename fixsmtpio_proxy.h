#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"

extern substdio sserr;

void strip_last_eol(stralloc *);
int ends_with_newline(stralloc *);
int is_last_line_of_response(stralloc *);
void parse_client_request(stralloc *,stralloc *,stralloc *);
int get_one_response(stralloc *,stralloc *);
int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *);
void construct_proxy_request(stralloc *,filter_rule *,char *,stralloc *,stralloc *,int,int *,int,int *,int *);
int is_last_line_of_data(stralloc *);

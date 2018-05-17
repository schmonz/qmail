#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"

extern substdio sserr;

extern void strip_last_eol(stralloc *);
extern int is_at_least_one_line(stralloc *);
extern int is_last_line_of_response(stralloc *);
extern void parse_client_request(stralloc *,stralloc *,stralloc *);
extern int get_one_response(stralloc *,stralloc *);
extern int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *);

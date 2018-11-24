#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"

void strip_last_eol(stralloc *);
int ends_data(stralloc *);
int is_last_line_of_response(stralloc *);
void parse_client_request(stralloc *,stralloc *,stralloc *);
int get_one_response(stralloc *,stralloc *);
int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *,int, char *);
void construct_proxy_request(stralloc *,filter_rule *,char *,stralloc *,stralloc *,int,int *,int,int *);
void start_proxy(stralloc *,filter_rule *,char **);

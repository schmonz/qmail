#include "fixsmtpio.h"
#include "fixsmtpio_filter.h"

void strip_last_eol(stralloc *);
int ends_data(stralloc *);
int is_last_line_of_response(stralloc *);
void parse_client_request(stralloc *,stralloc *,stralloc *);
int get_one_response(stralloc *,stralloc *);
int read_and_process_until_either_end_closes(int,int,int,int,stralloc *,filter_rule *,stralloc *,int);
void construct_proxy_request(stralloc *,filter_rule *,const char *,stralloc *,stralloc *,int,int *,int,int *);
void be_proxy(stralloc *,filter_rule *,char **);

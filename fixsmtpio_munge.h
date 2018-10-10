#include "fixsmtpio_filter.h"

void munge_exitcode(int *,filter_rule *);
void munge_greeting(stralloc *,int,stralloc *);
void munge_helo(stralloc *,int,stralloc *);
void munge_ehlo(stralloc *,int,stralloc *);
void munge_help(stralloc *,int,stralloc *);
void munge_quit(stralloc *,int,stralloc *);
void reformat_multiline_response(stralloc *);
int event_matches(char *,const char *);
void munge_line_internally(stralloc *,int,stralloc *,const char *);
void change_every_line_fourth_char_to_dash(stralloc *);
void change_last_line_fourth_char_to_space(stralloc *);

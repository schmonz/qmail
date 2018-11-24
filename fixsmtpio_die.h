#include "stralloc.h"

void die_usage(void);
void die_control(void);
void die_pipe(void);
void die_fork(void);
void die_exec(void);
void die_wait(void);
void die_kill(void);
void die_crash(void);
void die_read(void);
void die_write(void);
void die_nomem(const char *,const char *);
void die_tls(void);
void die_parse(void);
void logit(stralloc *,char,stralloc *);

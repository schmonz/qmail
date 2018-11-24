#include "fixsmtpio.h"
#include "fixsmtpio_die.h"
#include "fixsmtpio_filter.h"
#include "fixsmtpio_proxy.h"

#include "acceptutils_stralloc.h"
#include "acceptutils_unistd.h"

static void load_smtp_greeting(stralloc *greeting,char *configfile) {
  if (control_init() == -1) die_control();
  if (control_rldef(greeting,configfile,1,(char *) 0) != 1) die_control();
}

static void cd_var_qmail() {
  if (unistd_chdir(auto_qmail) == -1) die_control();
}

int main(int argc,char **argv) {
  stralloc greeting = {0};
  filter_rule *rules;

  argv++; if (!*argv) die_usage();

  if (env_get("NOFIXSMTPIO")) unistd_execvp(*argv,argv);

  stralloc_set_die(die_nomem);

  cd_var_qmail();
  load_smtp_greeting(&greeting,"control/smtpgreeting");
  rules = load_filter_rules();

  start_proxy(&greeting,rules,argv);
}

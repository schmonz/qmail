#include "hasblacklist.h"

#if HASBLACKLIST

#include <blacklist.h>

void pfilter_notify(int what,const char *msg) {
	blacklist(what, 0, msg);
}

#else

void pfilter_notify(int what,const char *msg) {
  ;
}

#endif

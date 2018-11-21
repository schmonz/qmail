#ifndef BASE64_H
#define BASE64_H

#include "stralloc.h"

int b64decode(const unsigned char *,int,stralloc *);
int b64encode(stralloc *,stralloc *);

#endif

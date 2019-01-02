#include "hasblacklist.h"

#if HASBLACKLIST

#include <blacklist.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

static int ip6(const struct sockaddr_storage *ss,char *ip,char *port) {
  struct sockaddr_in6 *sock = (struct sockaddr_in6 *)ss;
  sock->sin6_family = AF_INET6;
  sock->sin6_port = htons(atoi(port));
  return inet_pton(AF_INET6, ip, &sock->sin6_addr);
}

static int ip4(const struct sockaddr_storage *ss,char *ip,char *port) {
  struct sockaddr_in *sock = (struct sockaddr_in *)ss;
  sock->sin_family = AF_INET;
  sock->sin_port = htons(atoi(port));
  return inet_pton(AF_INET, ip, &sock->sin_addr);
}

static void socket_info(const struct sockaddr_storage *ss,socklen_t *slen) {
  memset((void *)ss, 0, *slen);
  char *proto = getenv("PROTO");
  if (proto && 0 == strcmp(proto,"TCP6")) {
    char *ip = getenv("TCP6REMOTEIP");
    char *port = getenv("TCP6LOCALPORT");
    if (!ip || !port || 0 == ip6(ss,ip,port))
      (void)ip6(ss,getenv("TCPREMOTEIP"),getenv("TCPLOCALPORT"));
  } else {
    char *ip = getenv("TCPREMOTEIP");
    char *port = getenv("TCPLOCALPORT");
    if (!ip || !port) return;
    if (0 == ip4(ss,ip,port))
      (void)ip6(ss,ip,port);
  }
}

void pfilter_notify(int what,const char *msg) {
  const struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  socket_info(&ss,&slen);
  blacklist_sa(what, 0, (void *)&ss, slen, msg);
}

#else

void pfilter_notify(int what,const char *msg) {
  ;
}

#endif

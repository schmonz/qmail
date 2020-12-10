#include "hasblacklist.h"

#if HASBLACKLIST

#include <blacklist.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define GEN_FILL_SOCKADDR(funcname,family,structname,familyfield,portfield,addrfield) \
static int funcname(const struct sockaddr_storage *ss,socklen_t *slen,char *ip,char *port) { \
  struct structname *sock = (struct structname *)ss; \
  sock->familyfield = family; \
  sock->portfield = htons(atoi(port)); \
  *slen = sizeof(*sock); \
  return inet_pton(sock->familyfield, ip, &sock->addrfield); \
}

GEN_FILL_SOCKADDR(ip6,AF_INET6,sockaddr_in6,sin6_family,sin6_port,sin6_addr);
GEN_FILL_SOCKADDR(ip4,AF_INET,sockaddr_in,sin_family,sin_port,sin_addr);

static void fill_sockaddr_ip6(const struct sockaddr_storage *ss,socklen_t *slen) {
  char *ip = getenv("TCP6REMOTEIP"); char *port = getenv("TCP6LOCALPORT");
  if (!ip || !port || 0 == ip6(ss,slen,ip,port))
    (void)ip6(ss,slen,getenv("TCPREMOTEIP"),getenv("TCPLOCALPORT"));
}

static void fill_sockaddr_maybe_ip4(const struct sockaddr_storage *ss,socklen_t *slen) {
  char *ip = getenv("TCPREMOTEIP"); char *port = getenv("TCPLOCALPORT");
  if (!ip || !port) return;
  if (0 == ip6(ss,slen,ip,port))
    (void)ip4(ss,slen,ip,port);
}

static void fill_sockaddr_info(const struct sockaddr_storage *ss,socklen_t *slen) {
  char *proto = getenv("PROTO");
  memset((void *)ss, 0, *slen);
  if (proto && 0 == strcmp(proto,"TCP6"))
    fill_sockaddr_ip6(ss,slen);
  else
    fill_sockaddr_maybe_ip4(ss,slen);
}

void pfilter_notify(int action,int fd,const char *msg) {
  const struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);

  fill_sockaddr_info(&ss,&slen);
  if (0 == blacklist_sa(action, fd, (void *)&ss, slen, msg))
    fprintf(stderr,"%s: blacklist_sa(%d, %d...)\n", msg, action, fd);
  else
    fprintf(stderr,"%s: blacklist_sa(%d, %d...) failed with errno %d\n", msg, action, fd, errno);
}

#else

void pfilter_notify(int action,int fd,const char *msg) {
  ;
}

#endif

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

#define AF_UNSPEC 0
#define AF_INET   2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

struct sockaddr {
  sa_family_t sa_family;
  char        sa_data[14];
};

struct sockaddr_storage {
  sa_family_t ss_family;
  char        __ss_pad[126];
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
